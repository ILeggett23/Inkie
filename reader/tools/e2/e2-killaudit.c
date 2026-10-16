/* SPDX-License-Identifier: AGPL-3.0-only
 * E2 phase E diagnostic (observation only, temporary): find who sends SIGKILL to the reader after the
 * USB cable is unplugged. "on" saves the current audit enable state, enables kernel auditing and adds one
 * exit rule logging kill/tkill/tgkill/pidfd_send_signal (aarch64). With no auditd running, the kernel
 * prints each record to the kernel log ("audit: type=1300 ... syscall=129 a0=<pid> a1=<sig> ... comm=...").
 * "off" deletes exactly that rule and restores the saved enable state. Nothing else is changed. */
#include <errno.h>
#include <linux/audit.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned seq;
static int talk(int fd, int type, const void* data, size_t len, void* reply, size_t replyLen) {
  struct { struct nlmsghdr h; unsigned char body[4096]; } m;
  memset(&m, 0, sizeof m);
  if (len > sizeof m.body) return -1;
  m.h.nlmsg_len = NLMSG_LENGTH(len); m.h.nlmsg_type = type; m.h.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK; m.h.nlmsg_seq = ++seq;
  memcpy(NLMSG_DATA(&m.h), data, len);
  struct sockaddr_nl to; memset(&to, 0, sizeof to); to.nl_family = AF_NETLINK;
  if (sendto(fd, &m, m.h.nlmsg_len, 0, (struct sockaddr*)&to, sizeof to) < 0) return -1;
  for (int tries = 0; tries < 4; ++tries) {
    unsigned char buf[8192]; ssize_t n = recv(fd, buf, sizeof buf, 0); if (n < 0) return -1;
    for (struct nlmsghdr* r = (struct nlmsghdr*)buf; NLMSG_OK(r, (unsigned)n); r = NLMSG_NEXT(r, n)) {
      if (r->nlmsg_seq != seq) continue;
      if (r->nlmsg_type == NLMSG_ERROR) { struct nlmsgerr* e = NLMSG_DATA(r); if (e->error) { errno = -e->error; return -1; } if (!reply) return 0; continue; }
      if (reply && r->nlmsg_type == type) { size_t c = r->nlmsg_len - NLMSG_HDRLEN; memcpy(reply, NLMSG_DATA(r), c < replyLen ? c : replyLen); return 0; }
    }
  }
  return reply ? -1 : 0;
}
static void rule(struct audit_rule_data* r) {
  memset(r, 0, sizeof *r);
  r->flags = AUDIT_FILTER_EXIT; r->action = AUDIT_ALWAYS;
  const int calls[] = {129 /* kill */, 130 /* tkill */, 131 /* tgkill */, 424 /* pidfd_send_signal */};
  for (unsigned i = 0; i < sizeof calls / sizeof calls[0]; ++i) r->mask[calls[i] / 32] |= 1u << (calls[i] % 32);
  r->field_count = 1; r->fields[0] = AUDIT_ARCH; r->values[0] = AUDIT_ARCH_AARCH64; r->fieldflags[0] = AUDIT_EQUAL;
}
int main(int argc, char** argv) {
  if (argc != 3 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) { fprintf(stderr, "usage: e2-killaudit on|off <state-file>\n"); return 64; }
  int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_AUDIT); if (fd < 0) { perror("KILLAUDIT socket"); return 1; }
  struct audit_rule_data r; rule(&r);
  struct audit_status s; memset(&s, 0, sizeof s);
  if (!strcmp(argv[1], "on")) {
    struct audit_status cur; memset(&cur, 0, sizeof cur);
    if (talk(fd, AUDIT_GET, NULL, 0, &cur, sizeof cur)) { perror("KILLAUDIT get"); return 1; }
    FILE* f = fopen(argv[2], "wx"); if (!f) { perror("KILLAUDIT state file"); return 1; } fprintf(f, "%u\n", cur.enabled); fclose(f);
    s.mask = AUDIT_STATUS_ENABLED; s.enabled = 1;
    if (talk(fd, AUDIT_SET, &s, sizeof s, NULL, 0)) { perror("KILLAUDIT enable"); return 1; }
    if (talk(fd, AUDIT_ADD_RULE, &r, sizeof r, NULL, 0)) { perror("KILLAUDIT add rule"); return 1; }
    printf("KILLAUDIT_ON previous_enabled=%u\n", cur.enabled); return 0;
  }
  unsigned previous = 0; FILE* f = fopen(argv[2], "r"); if (f) { if (fscanf(f, "%u", &previous) != 1) previous = 0; fclose(f); }
  int del = talk(fd, AUDIT_DEL_RULE, &r, sizeof r, NULL, 0);
  s.mask = AUDIT_STATUS_ENABLED; s.enabled = previous;
  int set = talk(fd, AUDIT_SET, &s, sizeof s, NULL, 0);
  printf("KILLAUDIT_OFF rule_deleted=%d restored_enabled=%u set=%d\n", del == 0, previous, set == 0);
  if (f) unlink(argv[2]);
  return del == 0 && set == 0 ? 0 : 1;
}

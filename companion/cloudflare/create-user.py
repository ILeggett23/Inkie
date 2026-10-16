#!/usr/bin/env python3
"""Prepare a private D1 account-creation SQL file; never executes remote SQL."""
import argparse,getpass,hashlib,os,pathlib,re,secrets,uuid
p=argparse.ArgumentParser();p.add_argument('email');p.add_argument('--output',type=pathlib.Path,default=pathlib.Path('work/create-user.sql'));args=p.parse_args()
email=args.email.strip().lower()
if not re.fullmatch(r'[^\s@]+@[^\s@]+\.[^\s@]+',email) or len(email)>254:p.error('Valid bounded email required')
password=getpass.getpass('New password (14–256 characters): ')
if not 14<=len(password)<=256 or password!=getpass.getpass('Repeat password: '):p.error('Passwords must match and meet length requirements')
salt=secrets.token_hex(16);encoded=salt+':'+hashlib.scrypt(password.encode(),salt=bytes.fromhex(salt),n=16384,r=8,p=1).hex();password=''
escape=lambda value:"'"+value.replace("'","''")+"'"
sql='INSERT INTO users(id,email,password) VALUES('+','.join(map(escape,[str(uuid.uuid4()),email,encoded]))+');\n'
args.output.parent.mkdir(parents=True,exist_ok=True);fd=os.open(args.output,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
with os.fdopen(fd,'w') as output:output.write(sql);output.flush();os.fsync(output.fileno())
print('Private account SQL prepared. Apply deliberately with Wrangler D1 execute --file; do not commit it. No remote action performed.')

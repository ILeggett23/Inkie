CREATE TABLE tom_deleted_conversations(user_id TEXT NOT NULL REFERENCES users(id),conversation_id TEXT NOT NULL,scope TEXT NOT NULL,created REAL NOT NULL,PRIMARY KEY(user_id,conversation_id,scope));
CREATE INDEX tom_conversation ON tom(user_id,conversation_id,created,id);

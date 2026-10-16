ALTER TABLE tom_illustrations ADD COLUMN parent_request_id TEXT;
CREATE INDEX tom_illustrations_parent ON tom_illustrations(parent_request_id);

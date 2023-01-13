SELECT table_schema,table_name
FROM information_schema.tables
WHERE table_schema ='default_service'
ORDER BY table_schema,table_name;

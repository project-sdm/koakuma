create table "users" (
  id uuid index btree_idx,
  name varchar,
  age int,
  birth_date date,
  married bool
) from file 'some/path';

select * from users;

select * from users
where id = '10';

select * from users
where some_important_value in (point(0, 0), radius 1.9);

select * from users
where other_important_value in (point(0, 0), k 727);

insert into users values
  ('some_id', 'some_name')
  ('other_id', 'other_name')
;

delete from users
where name = 'pepito';

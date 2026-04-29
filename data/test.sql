create table "users" (
  id uuid primary key,
  name varchar,
  age int index btree_idx,
  birth_date date,
  married bool
);

insert into
  users
values
  ('id_1', 'name_1', 18, '28-04-2026', true),
  ('id_2', 'name_2', 23, '22-04-2022', false),
  ('id_3', 'name_3', 27, '25-04-2009', true),
  ('id_4', 'name_4', 12, '28-04-1981', false),
  ('id_5', 'name_5', 40, '18-03-2003', true),
  ('id_6', 'name_6', 12, '28-04-1975', false);

select
  *
from
  users;

select
  *
from
  users
where
  age between 18 and 30;

select
  *
from
  users
where
  married = true;

select
  *
from
  users
where
  name = 'name_3';

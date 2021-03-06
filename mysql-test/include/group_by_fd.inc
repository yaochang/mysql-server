--source include/have_optimizer_trace.inc

let $DEFAULT_TRACE_MEM_SIZE=1048576; # 1MB
eval set optimizer_trace_max_mem_size=$DEFAULT_TRACE_MEM_SIZE;

set end_markers_in_json=on;
set optimizer_trace="enabled=on";

SET @old_sql_mode = @@sql_mode;
SET @@sql_mode='ONLY_FULL_GROUP_BY';

--echo #
--echo # Bug#16021396 ONLY_FULL_GROUP_BY REJECTS VALID QUERY USING VIEW
--echo #
create table t1(a int, b int, c int) engine=InnoDB;
let $count=2;
while($count)
{
if ($count == 2)
{
let $viewtype=merge;
}
if ($count == 1)
{
let $viewtype=temptable;
}
dec $count;
eval create algorithm=$viewtype view v1 as select t1.a*2 as a, t1.b*2 as b, t1.c*2 as c from t1;
show create view v1;
select sin(b) as z from t1 group by sin(b);
select sin(b) as z from v1 group by sin(b);
select sin(b) as z from t1 group by b;
select sin(b) as z from v1 group by b;
select sin(b) as z from v1 group by z;
drop view v1;
}
drop table t1;

# From testcase of Bug#16903135:

CREATE TABLE group_by_test2 (
 id int unsigned primary key,
 cat int unsigned not null,
 name varchar(10),
 num int unsigned
);

INSERT INTO group_by_test2 (id,cat,name,num) VALUES
(1,10,'foo',2),
(2,11,'foo',1),
(3,22,'bar',3),
(4,23,'bar',7),
(5,34,'test',7);

let $query=
SELECT
 cat,
 name,
 SUM(num)
FROM
 group_by_test2
GROUP BY
 cat;

--error ER_WRONG_FIELD_WITH_GROUP
eval $query;

ALTER TABLE group_by_test2 ADD UNIQUE INDEX (cat);
eval $query;
--error ER_WRONG_FIELD_WITH_GROUP
eval $query WITH ROLLUP;
# Expressions of group columns are ok:
SELECT
 cat,
length(cat),
 SUM(num)
FROM
 group_by_test2
GROUP BY
 cat
WITH ROLLUP;

DROP TABLE group_by_test2;

--echo # Test from Bug #18993257 SELECT AGGR + NON-AGGR FROM JOIN WITH VIEW IS NOT REJECTED BY ONLY_FULL_GROUP_BY

CREATE TABLE t1 (
  col_int_key INT,
  col_varchar_key VARCHAR(1)
);

CREATE TABLE t2 (
    pk INTEGER,
    col_int_key INTEGER
);

CREATE VIEW view_b AS SELECT * FROM t2;

--error ER_MIX_OF_GROUP_FUNC_AND_FIELDS
SELECT MIN( alias2.col_int_key ),
       alias2.col_int_key
FROM t1 AS alias1, t2 AS alias2
WHERE alias1.col_int_key IS NULL;

# Same with view:
--error ER_MIX_OF_GROUP_FUNC_AND_FIELDS
SELECT MIN( alias2.col_int_key ),
       alias2.col_int_key
FROM t1 AS alias1, view_b AS alias2
WHERE alias1.col_int_key IS NULL;

DROP TABLE t1, t2;
DROP VIEW view_b;

--echo #
--echo # WL#2489; Recognizing some functional dependencies
--echo #

--source include/show_json_object.inc
let $show_trace=
SELECT show_json_object('"functional_dependencies_of_GROUP_columns": {', TRACE)
FROM INFORMATION_SCHEMA.OPTIMIZER_TRACE;

create table t1(
a int,
b int not null,
c int not null,
d int,
unique key(b,c),
unique key(b,d)
);

--error ER_WRONG_FIELD_WITH_GROUP
select sin(a) as z from t1 group by b;
--error ER_WRONG_FIELD_WITH_GROUP
select sin(a) as z from t1 group by d,b;

# {b,c} UNIQUE NOT NULL => {c,b}->a
select sin(a) as z from t1 group by c,b;
select sin(a+b*c) as z from t1 group by c,b;
--echo # In PS mode, we see nothing below, because only_full_group_by
--echo # checks are done at PREPARE, whereas trace below is from EXECUTE.
eval $show_trace;

# With outer references:
--error ER_WRONG_FIELD_WITH_GROUP
select (select sin(a)) as z from t1 group by d,b;
select (select sin(a)) as z from t1 group by c,b;
# If key columns are in function, functional dependency disappears
--error ER_WRONG_FIELD_WITH_GROUP
select sin(a) as z from t1 group by c*2,b*2;

# FDs are recognized, like in SQL standard:
# b=2 => c->{b,c} => c->a as (b,c) is unique not null.
select sin(a) as z from t1 where b=2 group by c;
eval $show_trace;

# t2.a=t1.a => {t1.b,t1.c}->t2.a
select sin(t2.a) as z from t1, t1 as t2
 where t2.a=t1.a group by t1.b,t1.c;
eval $show_trace;

# t2.b=t1.b and t2.c=t1.c => {t1.b,t1.c}->{all cols of t2}
select sin(t2.a) as z from t1, t1 as t2
 where t2.b=t1.b and t2.c=t1.c group by t1.b,t1.c;
eval $show_trace;

# t2.b=t1.b and t2.c=t1.c => {t1.b,t1.c}->{all cols of t2}
# Moreover, {t1.b,t1.c}->{t1.d}.
# So t3.b=t2.b and t3.c=t1.d => {t1.b,t1.c}->{all cols of t3}.
select t3.a from t1, t1 as t2, t1 as t3
where
 t3.b=t2.b and t3.c=t1.d and
 t2.b=t1.b and t2.c=t1.c
group by t1.b,t1.c;
eval $show_trace;

# 3 tables:
# {t1.b,t1.c}->{t1.*}->{t2.b,t2.c}->{t2.*}->{t3.pk}->{t3.b}
create table t3(pk int primary key, b int);
select t3.b from  t1,t1 as t2,t3
  where t3.pk=t2.d and t2.b=t1.b and t2.c=t1.a
  group by t1.b,t1.c;
drop table t3;

# With subq
select (select t1.b from t1
  where t2.b=t1.b
  group by t1.a) from t1 as t2;

# Outer join.
create table t2 like t1;
delete from t1;
insert into t1 (a,b) values(1,10),(2,20);
insert into t2 (a,b) values(1,-10);
# In result, t2.a is NULL for both rows, values of t1.a are 1 and 2
select t1.a,t2.a from t1 left join t2 on t2.a=t1.a and t2.b=t1.b;
# So this query would choose one arbitrary value of t1.a - wrong:
--error ER_WRONG_FIELD_WITH_GROUP
select t1.a from t1 left join t2 on t2.a=t1.a and t2.b=t1.b group by t2.a;

# Also problem for FD with constant:
select t1.a,t2.a from t1 left join t2 on 42=t1.a and t2.b=t1.b;
--error ER_WRONG_FIELD_WITH_GROUP
select t1.a from t1 left join t2 on 42=t1.a and t2.b=t1.b group by t2.a;

--error ER_WRONG_FIELD_WITH_GROUP
select t1.a from t1 left join t2 on t2.b=t1.a group by t2.b;
--error ER_WRONG_FIELD_WITH_GROUP
select t1.a from t1 left join t2 on 42=t1.a group by t2.a;

--error ER_WRONG_FIELD_WITH_GROUP
select t1.c from t1 left join t2 on t1.a=t1.c group by t1.a;
--error ER_WRONG_FIELD_WITH_GROUP
select t1.b from t1 left join t2 on t1.c=t1.b and t1.a=t1.c group by t1.a;
select t1.a,t2.c from t1 left join t2 on t1.a=t2.c
and cos(t2.c+t2.b)>0.5 and sin(t1.a+t2.d)<0.9 group by t1.a;

# with keys:
select t1.a,t2.d from t1 left join t2 on t1.a=t2.c and t1.d=t2.b
and cos(t2.c+t2.b)>0.5 and sin(t1.a+t2.d)<0.9 group by t1.a,t1.d;

# with non-determinism:
--error ER_WRONG_FIELD_WITH_GROUP
select t1.a,t2.c from t1 left join t2 on t1.a=t2.c
and cos(t2.c+rand())>0.5 group by t1.a;
select t1.a, ANY_VALUE(t2.c) from t1 left join t2 on t1.a=t2.c
and cos(t2.c+rand())>0.5 group by t1.a;

# with parameter:
prepare s from 'select t1.a,t2.c from t1 left join t2 on t1.a=t2.c
and cos(t2.c+ ? )>0.5 group by t1.a';
execute s using @dummy;

# No OR
--error ER_WRONG_FIELD_WITH_GROUP
select t1.a,t2.c from t1 left join t2 on t1.a=t2.c
and cos(t2.c+t2.b)>0.5 OR sin(t2.d)<0.9 group by t1.a;

# With subq
--error ER_WRONG_FIELD_WITH_GROUP
select t2.b from t1 left join t1 as t2 on t1.a=t2.b and t1.b group by t1.a;
--error ER_WRONG_FIELD_WITH_GROUP
select t2.b from t1 left join t1 as t2 on t1.a=t2.b and (select t1.b) group by t1.a;

# Test ANY_VALUE:
select ANY_VALUE(t1.b) from t1 left join t2 on t1.c=t1.b and t1.a=t1.c group by t1.a;
--error ER_WRONG_FIELD_WITH_GROUP
select 3+(5*t1.b) from t1 left join t2 on t1.c=t1.b and t1.a=t1.c group by t1.a;
select 3+(5*ANY_VALUE(t1.b)) from t1 left join t2 on t1.c=t1.b and t1.a=t1.c group by t1.a;
delete from t1;
insert into t1 (a,b) values(1,10),(1,20),(2,30),(2,40);
--error ER_MIX_OF_GROUP_FUNC_AND_FIELDS
select a, sum(b) from t1;
select any_value(a), sum(b) from t1;
# different order of input rows, different "any_value":
select any_value(a), sum(b) from (select * from t1 order by a desc) as d;
--error ER_WRONG_FIELD_WITH_GROUP
select a,b,sum(c) from t1 group by a;
select a,any_value(b),sum(c) from t1 group by a;
select a,any_value(b),sum(c)
from (select * from t1 order by a desc, b desc) as d
group by a;

# With view.

let $count=3;
while($count)
{
let $source_query=select t1.a*2 as a, t1.b as b, t1.c as c, t1.d as d, t1.a as e from t1;
if ($count == 3)
{
--echo # Merged view
eval create algorithm=merge view v1 as $source_query;
show create view v1;
let $source=v1;
let $source_no_alias=v1;
}
if ($count == 2)
{
--echo # Materialized view
eval create algorithm=temptable view v1 as $source_query;
show create view v1;
let $source=v1;
let $source_no_alias=v1;
}
if ($count == 1)
{
--echo # Derived table
let $source=($source_query) as v1;
let $source_no_alias=($source_query);
}

--error ER_WRONG_FIELD_WITH_GROUP
eval select sin(a) as z from $source group by b;
--error ER_WRONG_FIELD_WITH_GROUP
eval select sin(a) as z from $source group by d,b;
eval select sin(a) as z from $source group by c,b;
--error ER_WRONG_FIELD_WITH_GROUP
eval select (select sin(a)) as z from $source group by d,b;
eval select (select sin(a)) as z from $source group by c,b;
--error ER_WRONG_FIELD_WITH_GROUP
eval select sin(a) as z from t1 group by c*2,b*2;
eval select sin(a+b*c) as z from $source group by c,b;
eval $show_trace;

eval select sin(a) as z from $source where b=2 group by c;

# {v1.b,v1.c}->v1.a->t2.a
eval select sin(t2.a) as z from $source, $source_no_alias as t2
 where t2.a=v1.a group by v1.b,v1.c;
eval $show_trace;

eval select sin(t2.a) as z from $source, $source_no_alias as t2
 where t2.b=v1.b and t2.c=v1.c group by v1.b,v1.c;

# With materialized view we get more dependencies than needed, due to implementation.
eval select t3.a from $source, $source_no_alias as t2, $source_no_alias as t3
where
 t3.b=t2.b and t3.c=v1.d and
 t2.b=v1.b and t2.c=v1.c
group by v1.b,v1.c;
eval $show_trace;

# If we simply went to real_item(), we would have WHERE 2*a=b, or
# GROUP BY 2*t1.a, so we would not find FDs. The original item
# (direct_view_ref here) must also be considered!
eval select a from $source where a=b group by b;
eval $show_trace;
eval select b from $source where a=b group by a;
eval $show_trace;

eval select v1.c from $source where v1.c=v1.a group by v1.a;
eval $show_trace;
eval select v1.a from $source group by v1.e;
eval $show_trace;
eval select v1.c from $source where v1.c=v1.a group by v1.e;
eval $show_trace;
# View appears only in WHERE
eval select t2.d from $source, t1 as t2 where v1.a=t2.d and v1.e=t2.a group by t2.a;
eval $show_trace;

drop view if exists v1;

# FD due to view's WHERE:

let $source_query=select t1.a*2 as a, t1.b as b from t1;
if ($count == 3)
{
eval create algorithm=merge view v1 as $source_query;
}
if ($count == 2)
{
eval create algorithm=temptable view v1 as $source_query;
}
if ($count == 1)
{
let $source=($source_query) as v1;
let $source_no_alias=($source_query);
}

--error ER_WRONG_FIELD_WITH_GROUP
eval select a from $source group by b;
drop view if exists v1;

let $source_query=select t1.a*2 as a, t1.b as b from t1 where t1.a=t1.b;
if ($count == 3)
{
--echo # Merged view
eval create algorithm=merge view v1 as $source_query;
}
if ($count == 2)
{
--echo # Materialized view
eval create algorithm=temptable view v1 as $source_query;
}
if ($count == 1)
{
--echo # Derived table
let $source=($source_query) as v1;
let $source_no_alias=($source_query);
}
eval select a from $source group by b;
eval $show_trace;

drop view if exists v1;

# Aggregates in view

let $source_query=select sum(t1.a) as a, sum(t1.b) as b from t1;
if ($count == 3)
{
--echo # Aggregates => skipping Merged view
}
if ($count == 2)
{
--echo # Materialized view
eval create algorithm=temptable view v1 as $source_query;
}
if ($count == 1)
{
--echo # Derived table
let $source=($source_query) as v1;
let $source_no_alias=($source_query);
}
if ($count <= 2)
{
eval select a from $source group by b;
eval $show_trace;
}

drop view if exists v1;

# Aggregates + GROUP BY in view

# We group by b*5, to show that it works with GROUP expressions, not only fields.
let $source_query=select a, b*5 as b, sum(t1.c) as c, sum(t1.d) as d from t1 group by a,b*5;
if ($count == 3)
{
--echo # Aggregates => skipping Merged view
}
if ($count == 2)
{
--echo # Materialized view
eval create algorithm=temptable view v1 as $source_query;
}
if ($count == 1)
{
--echo # Derived table
let $source=($source_query) as v1;
let $source_no_alias=($source_query);
}
if ($count <= 2)
{
--error ER_WRONG_FIELD_WITH_GROUP
eval select a from $source group by b;
--error ER_WRONG_FIELD_WITH_GROUP
eval select c from $source group by b;
--error ER_WRONG_FIELD_WITH_GROUP
eval select c from $source group by b,d;
--error ER_WRONG_FIELD_WITH_GROUP
eval select a,c from $source group by a;
eval select d,c from $source group by b,a;
eval $show_trace;
}

drop view if exists v1;

dec $count;
}

# Derived table in merged view
create algorithm=temptable view v2
as select a as a, 2*a as b from t1;
create algorithm=merge view v1
 as select v2.a as a, 3*v2.b as b from v2;
select v1.b from v1 group by v1.a;
eval $show_trace;

select t2.a from t1 as t2, v1 where t2.a=v1.b group by v1.a;
eval $show_trace;

# FDs in a view are those of the underlying query expression.
# FDs in a query expression: expressions in the SELECT list must be
# deterministic.
drop view v1;
create algorithm=merge view v1
 as select v2.a as a, rand()*v2.b as b from v2;
--error ER_WRONG_FIELD_WITH_GROUP
select v1.b from v1 group by v1.a;
drop view v1;
create algorithm=temptable view v1
 as select v2.a as a, rand()*v2.b as b from v2;
--error ER_WRONG_FIELD_WITH_GROUP
select v1.b from v1 group by v1.a;

drop view v1,v2;

# Item_direct_view_ref pointing to Item_direct_view_ref pointing to
# Item_field (a rare case!)
create algorithm=merge view v2 as select 2*a as a, 2*b as b from t1;
create algorithm=merge view v1 as select a, 3*b as b from v2 where a=b;
select 1 from (select a,b+0 from v1 group by a) as d;
drop view v1,v2;

# Some fun cases with aggregates in derived table.

# Inner query is valid: t1.a -> t1.b (equality in WHERE). Outer query:
# d.b -> t1.b (underlying column of d.b) -> t1.a (equality)
# -> sum(1) (because t1.a is all group columns so determines
# sum()) -> d.s (because sum() is underlying of d.s)
select d.s from
 (select b, sum(1) as s from t1 where a=b group by a) as d
group by d.b;

alter table t1 add column pk int primary key auto_increment;
# Inner query: t1.pk -> t1.* (pk). Outer query:
# d.b,d.c -> t1.b,t1.c (their underlying columns) -> t1.pk (because 
# t1.b,t1.c is unique) -> sum(1) (because t1.pk is all group columns so
# determines sum()) -> d.s (because sum() is underlying of d.s)
select d.s from
 (select b, c, sum(d) as s from t1 group by pk) as d
group by d.b,d.c;

# Outer query:
# d.c -> t1.b*3 (underlying column of d.c) -> sum(a) (because t1.b*3
# is all group expressions) -> d.s
select d.s from
 (select b*3 as c, sum(a) as s from t1 group by b*3) as d
group by d.c;

drop table t1,t2;


# Testcase from Reviewers

create table customer1(pk int primary key, a int);
create table customer2(pk int primary key, b int);
CREATE algorithm=merge VIEW customer as SELECT pk,a,b
FROM customer1 JOIN customer2 USING (pk);

let $query=select customer.pk, customer.b
from customer
group by customer.pk;

eval $query;
# View is merged. Show FDs. Note that in --ps-protocol, the trace
# is that of execution, so contains no group-by checks.
eval $show_trace;

drop view customer;
CREATE algorithm=temptable VIEW customer as SELECT pk,a,b
FROM customer1 JOIN customer2 USING (pk);
eval $query;
eval $show_trace;

# Benefit from outer-join-to-inner conversion.
insert into customer1 values(0,10),(1,20);
insert into customer2 values(0,10),(1,20);

# 1) no conversion, no FD from customer2.b to customer1.a.
--error ER_WRONG_FIELD_WITH_GROUP
explain select customer1.a, count(*)
 from customer1 left join customer2 on customer1.a=customer2.b
 where customer1.pk in (7,9)
 group by customer2.b;

# 2) null-complemented row can't pass WHERE => conversion
# => FD from customer2.b to customer1.a.
explain select customer1.a, count(*)
 from customer1 left join customer2 on customer1.a=customer2.b
 where customer2.pk in (7,9)
 group by customer2.b;

drop table customer1,customer2;
drop view customer;

# FDs of JOIN...USING and NATURAL JOIN

create table t1(pk int primary key, a int);
create table t2(pk int primary key, b int);

select t1.pk, t1.a from t1 join t2 on t1.pk=t2.pk group by t1.pk;
select t1.pk, t1.a from t1 join t2 using(pk) group by t1.pk;
select t1.pk, t1.a from t1 natural join t2 group by t1.pk;
select t1.pk, t1.a from t1 left join t2 using(pk) group by t1.pk;
select t1.pk, t1.a from t1 natural left join t2 group by t1.pk;

select t1.pk, t2.b from t1 join t2 on t1.pk=t2.pk group by t1.pk;
select t1.pk, t2.b from t1 join t2 using(pk) group by t1.pk; 
select t1.pk, t2.b from t1 natural join t2 group by t1.pk;
select t1.pk, t2.b from t1 left join t2 using(pk) group by t1.pk; 
eval $show_trace;
select t1.pk, t2.b from t1 natural left join t2 group by t1.pk;

# Equivalent queries, with RIGHT JOIN
select t1.pk, t2.b from t2 right join t1 using(pk) group by t1.pk; 
select t1.pk, t2.b from t2 natural right join t1 group by t1.pk;

--error ER_WRONG_FIELD_WITH_GROUP
select t1.pk, t2.b from t1 left join t2 on t1.pk>t2.pk group by t1.pk; 
# Even in --ps-protocol we see the group-by checks in trace because
# there has been no execution (due to error).
eval $show_trace;

--error ER_WRONG_FIELD_WITH_GROUP
select t1.pk, t2.b from t2 right join t1 on t1.pk>t2.pk group by t1.pk; 

drop table t1,t2;

# Cases where FDs from weak side do not propagate

create table t1(a int, b int);
insert into t1 values(null,0),(null,1);

let $query=
select d.a,d.c
from t1 left join (select a, coalesce(a,3) as c from t1) as d
on t1.b>0;
eval $query;
# Now group it by d.a:
--error ER_WRONG_FIELD_WITH_GROUP
eval $query group by d.a;
eval $show_trace;

let $query=
select d.a,d.c
from t1 left join (select a, count(a) as c from t1 group by a) as d
on t1.b+d.c>0;
eval $query;
# Now group it by d.a:
--error ER_WRONG_FIELD_WITH_GROUP
eval $query group by d.a;
eval $show_trace;

let $query=
select d.m,d.c
from t1 left join (select max(a) as m, count(a) as c from t1) as d
on t1.b+d.c>0;
eval $query;
# Now group it by d.m:
--error ER_WRONG_FIELD_WITH_GROUP
eval $query group by d.m;
eval $show_trace;
# Now group it by d.c which is non-nullable:
eval $query group by d.c;
eval $show_trace;

drop table t1;
create table t1(pk int primary key, a int);

# Not valid per the standard, because:
# 1) In t3, t3.pk->t3.a holds.
# 2) In R1 the result of "(t2 left join t3 on 1)", t3.pk->t3.a
# holds, by application of: there is a functional dependency in the
# weak side t3, and t3.pk is not nullable in t3.
# 3) In R2 the result of "t1 left join (t2 left join t3 on 1) on 1",
# t3.pk->t3.a doesn't hold anymore, because: it's a dependency in the
# weak side (weak side is R1), and t3.pk is nullable _when
# seen as a column of R1_ (in R1 t3.pk can be NULL, if the row of t3
# is actually a null-complemented one).
# But for us it is valid, because we have refined the logic: the
# pk-based FD satisfies the requirement that a NULL value of t3.pk
# implies a NULL value of t3.a (indeed, the NULL value of t3.pk can
# only come from null-complementing of the row of t3 in R1, in which
# case t3.a is also NULL).
select t3.a
 from t1 left join (t1 as t2 left join t1 as t3 on 1) on 1
 group by t3.pk;
eval $show_trace;

# Outer reference - why we use resolved_used_tables():
select (select t1.a from t1 as t2 limit 1) from t1 group by pk;

# We don't build the FD list if not needed
select t1.a*3 from t1 group by t1.a;
eval $show_trace;

drop table t1;

# Tricky cases with "ON col=literal" propagating.
create table t1(a int, b int);
insert into t1 values();
insert into t1 values(10,11);
create table t2(c int, d int);
insert into t2 values(2,3);

let $query=
select t4.d
 from t1 left join (t2 as t3 join t2 as t4 on t4.d=3) on t1.a=10;
eval $query;
# Equivalent to T1 LJ (T2, T3) ON T4.D=3 AND T1.A=10
# (this is what simplify_joins() does).
# For T4.D=3, DJS is {T1.A} which is not group column.
--error ER_WRONG_FIELD_WITH_GROUP
eval $query group by "";

let $query=
select t4.d
 from t1 left join (t2 as t3 left join t2 as t4 on t4.d=3) on t1.a=10;
eval $query;
# For T4.D=3, DJS={}, not NULL-friendly, and embedding is on weak side
# so FD cannot propagate.
--error ER_WRONG_FIELD_WITH_GROUP
eval $query group by "";

let $query=
select t4.d
 from t1 join (t2 as t3 left join t2 as t4 on t4.d=3) on t1.a=10;
eval $query;
# For T4.D=3, DJS={}, not NULL-friendly, but embedding is on weak side
# so FD can propagate.
eval $query group by "";

# With a view

create view v1 as select a as a, 2*a as b, coalesce(a,3) as c from t1;

let $query=
select v1.b from t1 left join v1 on 1;
eval $query;
# If v1.a is NULL then v1.b is NULL: a->b is NULL-friendly
eval $query group by v1.a;

let $query=
select v1.c from t1 left join v1 on 1;
eval $query;
# If v1.a is NULL then v1.c may not be NULL: a->c is not NULL-friendly
--error ER_WRONG_FIELD_WITH_GROUP
eval $query group by v1.a;

drop view v1;

# Constant view item
create view v1 as select a as a, 2 as b from t1;
let $query=
select t1.a, v1.b from t1 left join v1 on t1.a is not null;
# Because of BUG#17023060, the result is wrong, should be
# [10,2 ; 10,2 ; NULL,NULL], which would show that {}->{v1.b} does not
# hold in the result, even though v1.b is constant (=2) in v1.
eval $query;
# We correctly reject this:
--error ER_WRONG_FIELD_WITH_GROUP
eval $query group by v1.a;

drop view v1;
drop table t1,t2;

create table emp(empno int, ename char(1), deptno int);
create table dept(deptno int primary key, dname char(1));
let $view_rest=
VIEW empdept AS
SELECT emp.empno, emp.ename, dept.deptno, dept.dname
FROM emp LEFT OUTER JOIN dept ON (emp.deptno = dept.deptno);

let $query=
SELECT dname, COUNT(*)
FROM empdept
GROUP BY deptno;

eval CREATE algorithm=merge $view_rest;
eval EXPLAIN $query;
# There is pk-based FD dept.Depno->dept.dname in dept
# and it propagates in the view-which-became-nest because it is
# NULL-friendly.
eval $query;
drop view empdept;

eval CREATE algorithm=temptable $view_rest;
eval EXPLAIN $query;
# There is pk-based FD dept.Depno->dept.dname in dept
# and it propagates in the materialized view because it is
# NULL-friendly, and then in the top query because the view is not in
# the weak side of an outer join.
eval $query;

# More tests, for code coverage.

# UNION in derived table
--error ER_WRONG_FIELD_WITH_GROUP
select emp.ename
from
(select 1 as empno union select 2) deriv,
emp
where emp.empno=deriv.empno
group by emp.empno;

drop view empdept;

# Make the key-searching loop meet view columns which don't wrap a
# column (CONCAT).

CREATE VIEW empdept AS
SELECT emp.empno, emp.ename, dept.deptno, concat("",dept.dname) as dname
FROM emp LEFT JOIN dept ON (emp.deptno = dept.deptno);

--error ER_WRONG_FIELD_WITH_GROUP
SELECT ename, COUNT(*) FROM empdept WHERE empno=dname and empno=deptno GROUP BY empno;

drop table emp,dept;
drop view empdept;

DROP FUNCTION show_json_object;

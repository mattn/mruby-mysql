# mruby-mysql

mruby-mysql is a mrbgems.
It provide an interface to mysql with mruby.

## install

When you use in your project, please add below to your `build_config.rb`.

```ruby
conf.gem :github => 'mattn/mruby-mysql'
```

## Description

```ruby
# Creates a new handle for accessing a mysql database
db = MySQL::Database.new('db_host', 'db_user', 'password', 'db_name')
```

you must supply 4 parameters.

## Usage

### execute_batch

```ruby
db.execute_batch 'create table foo(id int primary key, text text, f float)'
```

```ruby
db.execute_batch('insert into foo(id, text) values(?, ?)', 1, 'foo')
```

when you want to use `create table`, `drop table`, `insert`, `update, delete` queries,
you need to use `execute_batch` method.


### execute

when you want to use `select` query,
you need to use `execute` method.

```ruby
db.execute('select * from foo') do |row, fields|
  puts fields # ["id", "text", "f"]
  puts row # [1, "foo", nil]
end
```

```ruby
row = db.execute('select * from foo')
while cols = row.next
  puts cols # [1, "foo", nil]
end
row.close
```

### transaction
This library supports transactions.

```ruby
# rollback
db.transaction
db.execute_batch('insert into foo(id, text) values(?, ?)', 2, 'baz')
db.rollback
```

```ruby
# commit
db.transaction
db.execute_batch('insert into foo(id, text) values(?, ?)', 2, 'baz')
db.commit
```

# License

This project is under the MIT License:

* http://www.opensource.org/licenses/mit-license.php

# Author

Yasuhiro Matsumoto (a.k.a. mattn)

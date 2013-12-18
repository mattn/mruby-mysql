#!mruby

db = MySQL::Database.new('localhost', 'root', '', 'foo')
begin
  db.execute_batch 'drop table foo'
  db.execute_batch 'drop table bar'
rescue ArgumentError
ensure
  db.execute_batch 'create table foo(id int primary key, text text, f float)'
  db.execute_batch 'create table bar(id int primary key, text text, f float)'
end

db.execute_batch('delete from foo')
db.execute_batch('insert into foo(id, text) values(?, ?)', 1, 'foo')
db.execute_batch('insert into foo(id, text) values(?, ?)', 2, 'bar')
db.transaction
db.execute_batch('insert into foo(text) values(?)', 'baz')
db.rollback
db.transaction
db.execute_batch('insert into foo(text) values(?)', 'bazoooo!')
db.commit

db.transaction
(1..100).each_with_index {|x,i|
  db.execute_batch('insert into bar(id, text) values(?,?)', i, x)
}
db.commit

db.execute('select * from bar') do |row, fields|
  puts row
end

#puts db.execute('select id from foo where text = ?', 'foo').next

db.execute_batch('delete from bar')
db.execute_batch('insert into bar(id, text, f) values(1,\'bababa\', NULL)')
db.execute_batch('insert into bar(id, text, f) values(2,\'bababa\', 3.14)')
db.execute('select * from bar') do |row, fields|
  puts row
end
row = db.execute('select * from bar')
puts row.fields
while cols = row.next
  puts cols
end
row.close

db.close

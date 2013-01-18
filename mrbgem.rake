MRuby::Gem::Specification.new('mruby-mysql') do |spec|
  spec.license = 'MIT'
  spec.authors = 'mattn'
 
  spec.cflags = `mysql_config --cflags`.delete("\n\r")
  if ENV['OS'] == 'Windows_NT'
    spec.mruby_libs = "#{`mysql_config --libs`.delete("\n\r")}"
  else
    spec.mruby_libs = `mysql_config --libs`.delete("\n\r")
  end
end

MRuby::Gem::Specification.new('mruby-mysql') do |spec|
  spec.license = 'MIT'
  spec.authors = 'mattn'
 
  spec.cc.flags = `mysql_config --cflags`.delete("\n\r")
  if ENV['OS'] == 'Windows_NT'
    spec.linker.flags = "#{`mysql_config --libs`.delete("\n\r")}"
  else
    spec.linker.flags = `mysql_config --libs`.delete("\n\r")
  end
end

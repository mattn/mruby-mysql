MRuby::Gem::Specification.new('mruby-mysql') do |spec|
  spec.license = 'MIT'
  spec.authors = 'mattn'
 
  spec.cc.flags << `mysql_config --cflags`.delete("\n\r").split(" ")
  if ENV['OS'] == 'Windows_NT'
    spec.linker.flags << "#{`mysql_config --libs`.delete("\n\r")}".split(" ").reverse
  else
    spec.linker.flags << `mysql_config --libs`.delete("\n\r").split(" ").reverse
  end
end

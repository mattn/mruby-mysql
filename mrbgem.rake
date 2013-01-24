MRuby::Gem::Specification.new('mruby-mysql') do |spec|
  spec.license = 'MIT'
  spec.authors = 'mattn'
 
  spec.cc.flags << `mysql_config --cflags`.delete("\n\r").split(" ")
  mysql_libs = "#{`mysql_config --libs`.delete("\n\r")}".split(" ")
  flags = mysql_libs.reject {|e| e =~ /^-l/ }
  libraries = mysql_libs.select {|e| e =~ /-l/ }.map {|e| e[2..-1] }
  spec.linker.flags << flags
  spec.linker.libraries << libraries
end

MRuby::Gem::Specification.new('mruby-mysql') do |spec|
  spec.license = 'MIT'
  spec.authors = 'mattn'
 
  spec.cc.flags << `mysql-config --cflags`.delete("\n\r").split(" ")
  mysql_libs = "#{`mysql-config --libs`.delete("\n\r")}".split(" ")
  flags = mysql_libs.reject {|e| e =~ /^-[lL]/ }
  libpaths = mysql_libs.select {|e| e =~ /^-L/ }.map {|e| e[2..-1].sub(/^\"(.*)\"$/) { $1 }}
  libraries = mysql_libs.select {|e| e =~ /^-l/ }.map {|e| e[2..-1]}
  spec.linker.flags << flags
  spec.linker.library_paths << libpaths
  spec.linker.libraries << libraries
end

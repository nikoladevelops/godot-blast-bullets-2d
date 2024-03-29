env = SConscript('godot-cpp/SConstruct')

env.Append(CPPATH=[
    "src/",
    "src/bullets",
    "src/spawn-data",
    "src/save-data",
    "src/factory", 
    "src/debugger",
    "src/shared"
    ])

src = (Glob("src/*.cpp") + 
Glob("src/bullets/*.cpp") + 
Glob("src/spawn-data/*.cpp") + 
Glob("src/save-data/*.cpp") + 
Glob("src/factory/*.cpp") + 
Glob("src/debugger/*.cpp") +
Glob("src/shared/*.cpp")
)

libpath = '../bin/lib_blast_bullets_2d{}{}'.format(env['suffix'], env['SHLIBSUFFIX'])
sharedlib = env.SharedLibrary(libpath, src)
Default(sharedlib)
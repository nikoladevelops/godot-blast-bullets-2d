import os

env = SConscript('godot-cpp/SConstruct') # Load godot_cpp configuration

env.Append(CPPATH=["src/"])  # Include the base 'src' directory

# Manually gather all .cpp files in 'src' and all its subdirectories
src = []
for root, dirs, files in os.walk("src"):
    for file in files:
        if file.endswith(".cpp"):
            src.append(os.path.join(root, file))


libpath = '../bin/lib_blast_bullets_2d{}{}'.format(env['suffix'], env['SHLIBSUFFIX'])
sharedlib = env.SharedLibrary(libpath, src)
Default(sharedlib)
import os

# Load the godot-cpp configuration
env = SConscript("godot-cpp/SConstruct")

# Add your source include path
env.Append(CPPPATH=["src/"])

# Gather all .cpp files in 'src' and subdirectories
src = []
for root, dirs, files in os.walk("src"):
    for file in files:
        if file.endswith(".cpp"):
            src.append(os.path.join(root, file))

# Define the output path
libpath = "../Test-Project-BlastBullets2D/addons/BlastBullets2D/bin/lib_blast_bullets_2d{}{}".format(env["suffix"], env["SHLIBSUFFIX"])

# Build the shared library
sharedlib = env.SharedLibrary(
    target=libpath,
    source=src
)

Default(sharedlib)
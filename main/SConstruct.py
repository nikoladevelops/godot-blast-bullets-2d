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

# This is here in order for the .xml documentation to be inserted inside the debug builds -> makes the documentation available inside the Godot Engine editor
if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData(
            "doc_classes/doc_data.gen.cpp", source=Glob("doc_classes/*.xml")
        )
        src.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

# Platform-specific library building
# Define the output path -> goes straight to the test project so it can be loaded
if env["platform"] == "macos":
    libpath = "../Test-Project-BlastBullets2D/addons/BlastBullets2D/bin/lib_blast_bullets_2d.{}.{}.framework/lib_blast_bullets_2d.{}.{}".format(
        env["platform"], env["target"], env["platform"], env["target"]
    )
    library = env.SharedLibrary(target=libpath, source=src)
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        libpath = "../Test-Project-BlastBullets2D/addons/BlastBullets2D/bin/lib_blast_bullets_2d.{}.{}.simulator.a".format(
            env["platform"], env["target"]
        )
        library = env.StaticLibrary(target=libpath, source=src)
    else:
        libpath = "../Test-Project-BlastBullets2D/addons/BlastBullets2D/bin/lib_blast_bullets_2d.{}.{}.a".format(
            env["platform"], env["target"]
        )
        library = env.StaticLibrary(target=libpath, source=src)
else:
    # Standard shared library for other platforms (Windows, Linux, etc.)
    libpath = "../Test-Project-BlastBullets2D/addons/BlastBullets2D/bin/lib_blast_bullets_2d{}{}".format(
        env["suffix"], env["SHLIBSUFFIX"]
    )
    library = env.SharedLibrary(target=libpath, source=src)

Default(library)

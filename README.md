

CURRENTLY UPDATING PLUGIN, README NEEDS TO BE EDITED
===========================
<p align="center">
  <img src="https://raw.githubusercontent.com/nikoladevelops/godot-blast-bullets-2d/main/blast_bullets_2d.png" alt="BlastBullets2D - Logo" width="200"/>
</p>


### Purpose
<b>BlastBullets2D</b> is a library written in <b>C++</b> for [Godot Engine](https://godotengine.org) that makes spawning and moving a huge amount of bullets a very efficient operation. Not only performance is increased <b>SIGNIFICANTLY</b>, but you also get the functionality
of <b>SAVING</b>/<b>LOADING</b> the bullets' state through easy to use `save()` and `load()` functions. 
The library comes pre-compiled for:
- <b>Windows (x86_64, arm64)</b>
- <b>Android (x86_64, arm64)</b>
- <b>Linux (x86_64)</b>
- <b>Web</b>

<b>BlastBullets2D</b> should work for <b>IOS</b> and <b>macOS</b> too, but you have to compile the code yourself.

The library is used inside your <b>Godot Engine</b> project just how you use any other <b>Node</b> and <b>Script</b>.
This means that you <b>DON'T NEED to know C++ at all</b> to use it! Everything is done by writing code in <b>GDScript</b> that calls the custom <b>C++</b> functions. This is made possible through Godot's [GDExtension](https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/what_is_gdextension.html) technology.

In short, use <b>BlastBullets2D</b> if you are looking for optimized bullets performance in <b>Godot</b> or if you want saving and loading of bullets' state. It is <b>EXTREMELY</b> more optimized compared to using an `Area2D` with an `AnimationPlayer`.

<details>
<summary><b>BlastBullets2D</b> Features</summary>

- Efficient rendering by using `MultiMeshInstance2D`
- Improved performance by using **object pooling**
- Improved performance by using only **C++** for everything, instead of **GDScript**
- Saving bullets state
- Loading bullets state
- Debugger for the collision shapes, so you can see what exactly is happening when changing collision related properties
- Speed, max_speed, acceleration
- Rotation speed, rotation max_speed, rotation acceleration
- Custom collision layers and collision masks by providing a bitmask
- Animation by providing multiple textures that switch over a period of time
- Custom texture size and collision shape size
- Custom collision shape offset
- Custom bullet max_life_time
- The ability to provide a custom `Material` and `Mesh` (if you want to use shaders)
- Easy to use `area_entered` and `body_entered` signals similar to `Area2D`
- The option of providing custom data for a bullet multimesh that is also automatically saved/loaded and also accessed through `area_entered` and `body_entered`. This is very useful when you have data like armor_damage, magic_damage, bullet_type or anything else you can think of that you want for the bullets to hold and apply to an enemy when it's hit

</details>

# Install instructions (FOR GODOT 4.1 AND 4.2)
1. Download zip and extract it. If however you plan on making changes and compiling the C++ code yourself in the future, then ensure that <b>godot_cpp</b> is also included by running this command:
```
git clone --recurse-submodules https://github.com/nikoladevelops/godot-blast-bullets-2d.git
```
2. Make sure the folder you got from extracting the zip or cloning is named `BlastBullets2D`. This is important.
2. Open your <b>Godot</b> game project.
3. Create folder named `addons` if it doesn't already exist.
4. Cut the folder `BlastBullets2D` and paste it inside the `addons` folder.
5. Close <b>Godot</b> and open the project again.

# How to use
1. Add a `BulletFactory2D` node to your scene tree. The BulletFactory's job is to spawn bullets <br> (I suggest creating an [Autoload/Singleton](https://docs.godotengine.org/en/stable/tutorials/scripting/singletons_autoload.html) so you can spawn bullets from any script).
2. Create a script.
3. Inside the script create a `BlockBulletsData2D` and set up its properties according to the documentation.
Example:
```
var data:BlockBulletsData2D = BlockBulletsData2D.new()
data.transforms = getNewMarkerTransforms() # a custom function that returns an array of transforms
data.textures = allTextures # an array of preloaded textures
var speed_data:Array[BulletSpeedData2D] = BulletSpeedData2D.generate_random_data(2, 100,200,250,250,500,1000);
data.all_bullet_speed_data=speed_data
data.collision_layer = BlockBulletsData2D.calculate_bitmask([1])
data.collision_mask = BlockBulletsData2D.calculate_bitmask([3])
data.texture_size = Vector2(64,64)
```
5. Use the factory's `spawnBlockBullets2D()` function every time you want to spawn bullets and provide a `BlockBulletsData2D` as an argument.
Example:
```
factory.spawnBlockBullets2D(data)
```
<b>You can view a [Demo Project](https://github.com/nikoladevelops/demo-project-blast-bullets-2d) to see how the library is used</b>

# Simple set up explanation
The mandatory properties that you need to set for `BlockBulletsData2D` are: `transforms` and `all_bullet_speed_data`.

The `transforms` property requires an array of `Transform2D`, where each entry determines the position and rotation of a bullet. The rotation of each transform determines the direction of the corresponding bullet, but only if the amount of transforms is the same amount of `BulletSpeedData2D` instances provided in `all_bullet_speed_data`.

`all_bullet_speed_data` expects an array of `BulletSpeedData2D`, each defining the properties `acceleration`, `speed`, and `max_speed`. You can create this array easily using the provided static method `BulletSpeedData2D.generate_random_data()`. Ensure that the number of `BulletSpeedData2D` instances matches the number of `Transform2D` entries to maintain individual bullet directions. Otherwise, all bullets will share the same direction determined by `data.block_rotation_radians`, moving as a block for better performance.

# Documentation
<details>
<summary>BlockBulletsData2D</summary>

### Texture Settings

- `textures`: Array containing textures. If more than one texture is provided, `max_change_texture_time` will be used to periodically change the texture.
- `texture_size`: Size of the texture (used if no mesh is provided). Default: `Vector2(32,32)`.
- `texture_rotation_radians`: Rotation of the texture in radians. Use if the texture is not rotated properly. Example: If you want to rotate the texture 90 degrees more then you would do `90*PI/180`
- `current_texture_index`: Index of the starting texture in the array. Default: `0`.
- `max_change_texture_time`: Time before changing the texture to the next one in the array. Default: `0.3f`.
- `is_texture_rotation_permanent`: Determines if texture rotation is permanent. By default the texture's rotation changes based on the bullet's rotation.
If for some reason you want the texture's rotation to never be affected then set this to true.Default:`false`.

### Bullet Movement

- `transforms`: Array determining rotation and position of each bullet. The rotation of each `Transform2D` determines the direction in which the corresponding bullet will travel <b>BUT ONLY</b> if `use_block_rotation_radians` is set to `false` <b>AND</b> if the amount of `BulletSpeedData2D` in `all_bullet_speed_data` is the same as the amount of `Transform2D` provided inside `transforms` (meaning you have `BulletSpeedData2D` for every bullet).
- `block_rotation_radians`: This is a rotation that determines the direction in which <b>ALL</b> bullets will travel as a block. It is used only when `use_block_rotation_radians` is set to `true`. Default: `0.0f`.
- `use_block_rotation_radians`: If `true`, forces all bullets to move as a block and only the first `BulletSpeedData2D` inside `all_bullet_speed_data` is used. <b>SIGNIFICANTLY BOOSTS PERFORMANCE</b> but the bullets will be moving with the same speed/max_speed/acceleration, so they may not look as good. The direction in which <b>ALL</b> bullets will move is determined by `block_rotation_radians`. Default: `false`.
- `all_bullet_speed_data`: Array providing speed data for each bullet. Use the static method `BulletSpeedData2D.generate_random_data()` to generate an array of `BulletSpeedData2D` easily.

### Bullet Rotation

- `all_bullet_rotation_data`: Optional array providing rotation data for each bullet. Populate this array with `BulletRotationData2D` if you want your bullets to spin.
Give only a single `BulletRotationData2D` if you want <b>ALL</b> your bullets to spin with the same speed/max_speed/acceleration. You should give the same amount of `BulletRotationData2D` as the size of `transforms` array if you want each bullet to spin with individual speed/max_speed/acceleration.
Use the static method `BulletRotationData2D.generate_random_data()` to easily generate `BulletRotationData2D`. If you don't provide at least 1 `BulletRotationData2D` <b>OR</b> if the amount of data is not the same as the amount of `Transform2D` inside `transforms` then all provided data will be ignored and your bullets <b>WILL NOT</b> rotate/spin.
- `rotate_only_textures`: By default only the textures are being rotated when `all_bullet_rotation_data` is populated. If for some reason you want the collision shapes to also rotate with the textures then set this to `false` (this will decrease performance). Default: `true`.

### Collision

- `collision_layer`: Bitmask for collision layer. Use the static method `BlockBulletsData2D.calculate_bitmask()` to easily get a bitmask. <b>NEVER</b> set this to 0 or negative number.
- `collision_mask`: Bitmask for collision mask. Use the static method `BlockBulletsData2D.calculate_bitmask()` to easily get a bitmask. <b>NEVER</b> set this to 0 or negative number.
- `collision_shape_size`: Size of collision shape (rectangle). Default: `Vector2(5,5)`. If you want your collision shape to be bigger/smaller then change this.
- `collision_shape_offset`: Offset of collision shape. If you want your collision shape to be positioned away from the center of the texture then change this.
- `monitorable`: If `true`, enables `StaticBody2D` detection. I suggest you <b>DO NOT</b> use this. It will make it possible for your bullets to detect `StaticBody2D`, but it <b>DECREASES PERFORMANCE A LOT</b>. A good workaround is to always have an `Area2D` on your static bodies, that has `monitorable` set to `true` and `monitoring` set to `false`. This `Area2D` will act as the place where bullets can hit. Note that even though `monitorable` is set to `false` by default, the bullets will still be able to interact with `CharacterBody2D` and `RigidBody2D` bodies, the exception is only `StaticBody2D`, so follow my advice.
- `bullets_custom_data`: Additional data for bullets. If you want your bullets to have damage or anything else specific then you do this -> Create a class script that extends Resource -> Put `@export` variables inside like damage/armor_damage or whatever else you need (the `@export` keyword is extremely important otherwise the data won't be saved!) -> Create a new instance of your class (example: MyCustomResource.new()) -> populate the properties -> pass it inside here. Congrats, now you can access your custom_data from the `area_entered` and `body_entered` function callbacks inside the `factory`!

Example of a <b>Custom Resource</b> class:
```
class_name DamageData
extends Resource

@export var base_damage:int
@export var armor_damage:int
@export var magic_damage:int
```
### Other

- `max_life_time`: Duration before bullets are disabled/dissapear. Default: `2.0f`.
- `material`: Custom material (maybe you want to use shaders?).
- `mesh`: Custom mesh (if provided, `texture_size` property is ignored, so handle resizing of the texture inside your shader).

### Utility

- `int calculate_bitmask(numbers:Array[int]) static`: Method to acquire a bitmask from an array of integers. <b>NEVER</b> pass 0 or negative numbers, it will cause issues.
</details>

<details>
<summary>BulletFactory2D</summary>
  
### Properties

- `physics_space`: The physics space where the bullets' collision shapes are interacting with the world. You don't really need to touch this unless you know what you are doing.

- `is_debugger_enabled`: Determines whether the collision shape debugger is enabled or not. When exporting your game or testing performance make sure that this is set to `false`, because it tanks performance. Use only when you want to debug your collision shapes (what happens when you increase a collision shape's size and see where the shape is positioned relative to the texture).

### Methods

- `void spawnBlockBullets2D(spawn_data:BlockBulletsData2D)`: Spawn bullets with the provided data.
- `SaveDataBulletFactory2D save()`: Saves the bullets' state and returns a `SaveDataBulletFactory2D` resource that you can save to a file. When the `SaveDataBulletFactory2D` resource is finished being populated with bullet state data, the `finished_saving` signal is emitted.
- `void load(new_data:SaveDataBulletFactory2D)`: Loads the bullets' state from a `SaveDataBulletFactory2D` resource. When the loading of bullets to the scene tree finishes, the `finished_loading` signal is emitted.
- `void clear_all_bullets()`: Clears all bullets from the object pool and also from the scene. Always call this method using `call_deffered()` to avoid your game crashing. When clearing finishes, the `finished_clearing` signal is emitted.

Watch this quick tutorial on <b>Custom Resources</b> to understand more of what this following code does and how you can implement your own custom <b>SAVING</b>/<b>LOADING</b> logic for the rest of your game: [Godot Custom Resources Tutorial](https://www.youtube.com/watch?v=fdRJqnOrz98) and read the [Godot Resources Documentation](https://docs.godotengine.org/en/stable/tutorials/scripting/resources.html)

Simple implementation of saving and loading:
```
@onready var factory:BulletFactory2D = $MyBulletFactory # get a reference to the factory node
var savePath:String = OS.get_user_data_dir() + "/test.tres"; # use the .res extension if you want it saved as binary data (I suggest looking into encryption for actual security, so the user won't be able to change damage/speed values and so on..)

# When the save button is pressed
func _on_save_btn_pressed():
  var data:SaveDataBulletFactory2D = factory.save(); # Get the bullets' state
  	
  var result = ResourceSaver.save(data, savePath)  # Saves the data to savePath and return whether it was successful
  if result == OK:
    # saving is successful
  else:
    # saving failed, handle the error

# When the load button is pressed
func _on_load_btn_pressed():
  var data:SaveDataBulletFactory2D = ResourceLoader.load(savePath) # get the data that is saved at savePath

  if data != null: 
    factory.call_deferred("clear_all_bullets") # clear all old bullets from the level
    factory.call_deferred("load", data) # load the bullets from the save file
  else:
    # handle error, data was null/ an error occured
```

Note that when saving your game's state by using `save()`, you have to ensure that the user won't spam click your save/load buttons which may cause invalid data to be saved to the `SaveDataBulletFactory2D` that gets returned.
To avoid such bugs, ensure that when saving/loading you lock the GUI menu's buttons that are used for saving/loading until that finishes (the `finished_loading` signal is very helpful in that case).

### Signals
- `area_entered(enemy_area: Object, bullets_custom_data: Resource, bullet_global_position: Vector2)`:
The `enemy_area` is the `Area2D` that got hit with the bullet.
Check `BlockBullets2D` documentation to see how to set up `bullets_custom_data` that can store damage and other additional properties.
The `bullet_global_position` is the last position the bullet had before dissapearing, so you can use it to spawn particles at the same place.

- `body_entered(enemy_body: Object, bullets_custom_data: Resource, bullet_global_position: Vector2)`:
Only active if `monitorable` of the bullets is set to `true`. Check `BlockBulletsData2D` for more info.
The `enemy_body` is the body that got hit with the bullet.
Check `BlockBulletsData2D` documentation to see how to set up `bullets_custom_data` that can store damage and other additional properties.
The `bullet_global_position` is the last position the bullet had before dissapearing, so you can use it to spawn particles at the same place.


Ensure that the enemy `Area2D` has `monitorable` set to `true` and also that the `collision_layer` and `collision_mask` for both the `Area2D` and the bullets are configured correctly.
The same applies for bodies too.

Example of implemented callbacks that use `area_entered` and `body_entered`:
```
func _on_bullet_factory_2d_area_entered(enemy_area, bullets_custom_data:Resource, bullet_global_position:Vector2):
  if bullets_custom_data is DamageData: # maybe you have bullets that have other bullets_custom_data types and you have individual logic for each?
    var actualEnemy = enemy_area.get_parent() # if the Area2D was added just how I recommended to AVOID setting monitorable to true, you can get the parent of the area which will be the static body you want to damage
    #if (actualEnemy is CustomEnemyType)
    # apply bonus damage or don't apply magic damage or any other complex logic
    # maybe check if actualEnemy.immunityArray() contains bullets_custom_data.type or something like that, you can do pretty much anything

    actualEnemy.take_damage(bullets_custom_data.armor_damage)

func _on_bullet_factory_2d_body_entered(enemy_body, bullets_custom_data:Resource, bullet_global_position:Vector2):
  if bullets_custom_data is DamageData:
    enemy_body.take_damage(bullets_custom_data.armor_damage)
```


- `finished_saving`: Emitted when `save()` method is done populating the `SaveDataBulletFactory2D`.
- `finished_loading`: Emitted when all bullets from `SaveDataBulletFactory2D` were added to the scene tree.
- `finished_clearing`: Emitted when all bullets were cleared/deleted from the object pool and from the scene tree.

### Things to keep in mind:

1. Object pooling is automatic, bullets are <b>NEVER DELETED</b>, instead they stay completely <b>DISABLED</b> in the scene tree until they are about to be re-used.
2. When saving, only the active bullets are being saved, which means that bullets that are in the pool are <b>NEVER SAVED</b>.
3. If you are switching game levels and you think that having too many disabled bullets impacts your performance in a bad way, instead of helping to increase your FPS, you can use the `clear_all_bullets()` function, which will clear <b>ALL ACTIVE BULLETS</b> in the scene tree <b>AND</b> <b>ALL DISABLED BULLETS</b> that are in the object pool.
4. In some cases it might be beneficial to first populate the object pool before starting your game level. You can use a `for` loop and the `spawnBlockBullets2D()` function for this task. Use the same exact data but with very little `max_life_time`, so that the bullets can instantly enter the object pool. Consider displaying a loading screen while the object pool is being populated.
An <b>EXTREMELY IMPORTANT</b> thing to know is that the object pool is actually pooling `MultiMeshInstance2D` nodes and the whole pooling mechanism relies on the `transforms.size()` of `BlockBulletsData2D` (meaning the amount of bullets a single multimesh has). If you populate your pool with `MultiMeshInstance2D` nodes that have `transforms.size()` equal to 30 (meaning each `MultiMeshInstance2D` node holds 30 bullets and let's say you spawn 550 of them to populate the pool), but in your game you frequently spawn bullets that are made out of only 20 `Transform2D` and you <b>RARELY</b> spawn bullets with `transforms.size()` equal to 30 exactly, then all those 550 `MultiMeshInstance2D` nodes won't be re-used until you use the spawn function with a `BlockBulletsData2D` that has a `transforms` array with `.size()` equal to <b>EXACTLY 30</b>.
In short, use the `spawnBlockBullets2D()` with `BlockBulletsData2D` that has `transforms.size()` equal to the most spawned bullets amount at once (if your enemies and player always very frequently spawn 20 bullets at once, then you would ensure the `transforms` array holds <b>20</b> `Transform2D`, before spam calling the `spawnBlockBullets2D()` to populate the object pool).
</details>

<details>
  <summary>Compilation instructions</summary>

1. Download source code with included <b>godot_cpp</b> submodule.

```
git clone --recurse-submodules https://github.com/nikoladevelops/godot-blast-bullets-2d.git
```

2. Install [Scons](https://scons.org/).
Easiest way is if you have [Python](https://www.python.org/) run this:
```
python -m pip install scons
```

3. Go to <b>main</b> folder where <b>SContrsuct.py</b> file is located.
Open your command terminal (Example: <b>cmd</b> on <b>Windows</b>) in the same directory then type one of these depending on the platform you are targeting (if you receive an error it means you don't have the required toolchain to compile for the platform you are targeting, so do some research on what you're missing):

### For Windows
```
scons platform=windows arch=x86_64 target=template_debug
```
```
scons platform=windows arch=x86_64 target=template_release
```

```
scons platform=windows arch=arm64 target=template_debug
```
```
scons platform=windows arch=arm64 target=template_release
```

### For Linux
```
scons platform=linux arch=x86_64 target=template_debug
```
```
scons platform=linux arch=x86_64 target=template_release
```

### For Android
Ensure you have an <b>Android SDK</b> (you can download <b>Android Studio</b> and get all the things you need from there). Here is some useful documentation [Compiling for Android](https://docs.godotengine.org/en/stable/contributing/development/compiling/compiling_for_android.html)
```
scons platform=android arch=x86_64 target=template_debug ANDROID_HOME=C:\Users\Admin\AppData\Local\Android\Sdk
```
```
scons platform=android arch=x86_64 target=template_release ANDROID_HOME=C:\Users\Admin\AppData\Local\Android\Sdk
```
```
scons platform=android arch=arm64 target=template_debug ANDROID_HOME=C:\Users\Admin\AppData\Local\Android\Sdk
```
```
scons platform=android arch=arm64 target=template_release ANDROID_HOME=C:\Users\Admin\AppData\Local\Android\Sdk
```
### For Web
You need [emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html). Put <b>emsdk</b> and emscripten's location inside environment variable <b>Path</b>.
Before trying to compile for web, each time you open your command terminal you need to run this
```
emsdk activate latest
```

After than run each of these:
```
scons platform=web target=template_debug
```
```
scons platform=web target=template_release
```

### For IOS and macOS
Due to me not having these OS-es I can only give you short instructions on what to look for and which files you need to edit.

First of all this whole library relies on <b>GDExtension</b> so it has a <b>.gdextension</b> file with path locations of the binaries it needs to load.
See [godot_cpp's .gdextension file](https://github.com/godotengine/godot-cpp/blob/master/test/project/example.gdextension) vs [mine](https://github.com/nikoladevelops/godot-blast-bullets-2d/blob/main/.gdextension).
Also notice how their <b>SConstruct</b> file differs from mine and add the missing logic: [theirs](https://github.com/godotengine/godot-cpp/blob/master/test/SConstruct) vs [mine](https://github.com/nikoladevelops/godot-blast-bullets-2d/blob/main/.gdextension).
Research on which toolchain you need for <b>IOS</b> and for <b>macOS</b>, download them and then run the same scons commands, but for your desired platform and other desired arguments.
See the official [GDExtension Documentation](https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/index.html) or search for some tutorials online. Sadly <b>GDExtenstion</b> is not well documented, so you might spend some time searching. I recommend joining <b>Godot's Discord Server</b> it has a <b>gdnative-gdextension channel</b> so you might find some help there.
  
</details>



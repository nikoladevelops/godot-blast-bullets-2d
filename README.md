<p align="center">
  <img src="showcase/logo-big.jpg" alt="BlastBullets2D - Logo" width="500"/>
</p>

## Purpose

**BlastBullets2D** is a high-performance, **free and open source C++ plugin** for [Godot Engine](https://godotengine.org) that enables **optimized bullet spawning and management** in 2D games. It’s designed to efficiently handle a large number of bullets with minimal performance overhead, making it **ideal for fast-paced, bullet-heavy gameplay**.

If you're searching for a **Godot optimized bullets plugin**, **BlastBullets2D** is built exactly for that purpose.

Also perfect as a **Godot bullet hell plugin** since it allows having **THOUSANDS of bullets visible on screen**.

BlastBullets2D comes already compiled and ready for these platforms:
- **Windows**
- **macOS**
- **Linux**
- **Android**
- **iOS**
- **Web**

**BlastBullets2D is fully cross platform now, because it uses the [godot-plus-plus template](https://github.com/nikoladevelops/godot-plus-plus)**. If you also want to write C++ code in Godot or you have some old GDExtension plugins you want to update, I suggest checking it out.

**BlastBullets2D** integrates seamlessly into your Godot project. You do not need any knowledge of C++ to use it. Everything is controlled through **GDScript**, made possible by Godot’s [GDExtension](https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/what_is_gdextension.html) system.

[GDExtension C++ Tutorial](https://www.youtube.com/watch?v=I79u5KNl34o&t=1s)

---

## Why Choose BlastBullets2D?

<p align="center">
  <img src="showcase/example.gif" alt="BlastBullets2D - Showcase Gif" width="500"/>
</p>

If you're developing a bullet-hell shooter or any game that involves a high number of 2D bullets, **BlastBullets2D** provides a powerful and easy-to-use solution that delivers **outstanding performance** inside Godot Engine.

The main advantages to using this custom built plugin:

- **Production Ready, Free, Open Source And Yours Forever Without Any Hidden Fees Or Weird Licenses** - If you like what I do and you find the plugin useful consider supporting further development of such tools on [KoFi](https://ko-fi.com/realnikich) and [Patreon](https://patreon.com/realnikich). Expect tutorials on [My Youtube Channel](https://www.youtube.com/@realnikich).

- **Superior Performance And Simple To Use API That Does NOT Require Any Math Knowledge**.

- **Homing Bullets** - Targets Node2D enemies, GlobalPositions and even the Mouse. You have support for shared homing targets and per-bullet homing targets with a double ended queue implementation. You can control the smoothness of the rotation, the update time interval for tracking the targets, register a callback function when the required distance away from the target has been reached and much much more!

- **Orbiting Bullets** - Each bullet can begin orbiting a homing target at a custom radius while also moving in a circle around it. Imagine a swarm of bullets orbiting your mouse as you move it, while the radius increases or decreases then some of them go and orbit another target, this is the type of behavior you can implement on the go.

- **Path2D Movement Patterns** - Draw a Path2D in your scene and suddenly the bullets possess that movement behavior
relative to their direction (allows zig zag patterns and any other creative pattern you can think about). Example: A bullet gets spawned with a zig zag pattern (that might or might not repeat), then you can swap it with another pattern during runtime or even have homing bullets with custom movement patterns - very powerful.

- **BulletCurvesData2D** - You can use curves inside the Inspector to control the speed, rotation and direction. Choose between using unit curves (normalized time) or absolute values (at exactly X time have Y speed) - super easy and creative way of having bulllets that speed up/slow down at specific times without the need of writing any extra code.

- **Control Speed And Rotation The Normal Way** - If bullet curves are not needed, you can still use normal speed and speed acceleration values for both movement and rotation using **BulletSpeedData2D** and **BulletRotationData2D**.

- **Animated Textures** - Support for animation that switches between an array of textures and texture times (how long the texture should be on screen before moving to the next). Set a default texture or have full blown animated bullets, no problem!

- **Custom Collision Shape Sizes** - Support for rectangle collision shapes with a custom Vector2 size.

- **Support For Custom Bullet Max Collisions Amount** - A bullet can collide multiple times before being disabled. Very useful for Bullet Hell games.

- **Custom Bullets Debugger** - Debug collision shapes easily to see what is going on in your game and find problems.

- **Attach Timer Logic** - The ability to attach function callbacks that execute at a particular time on the entire multimesh with the option to be repeated over and over (safely executes code during runtime while preventing crashes that might occur with the normal timers if you don't use `call_deferred()`). This is the preferred way of manipulating bullet related data, don't use the normal Godot timers!

- **Edit Properties And Call Methods During Runtime** - Every single feature has helper methods that you can use during runtime to adjust the behavior of the bullets (change homing targets, orbiting radius, switch movement pattern, disable a bulllet and so on..). Combine this with the attach timer logic and you have the most flexible bullet system ever created. If you are not using the attach timer logic, make sure to use `call_deferred()` to avoid issues like game crashes or inconsistent behavior.

- **Teleport And Offset Support** - Instantly shift bullets with a Vector2 offset value or teleport them in global space to a new position.

- **Set Inherited Velocity Offset** - This is the velocity that gets added to the bullets when they are spawned. Useful for creating effects like a machine gun that inherits the velocity of the player character, so that the bullets shoot out with more speed if the player is moving forward, and with less speed if the player is moving backward or standing still.

- **Instance Shader Parameters** - Support for shaders with instance uniform variables.

- **Custom Physics Interpolation logic** - Bullets will always look smooth no matter the refresh rate of the monitor/device that you are targeting. No more weird jitter and buggy choppy feel on screens above 60Hz.

- **Bullet Attachments** - Seamlessly attach GPUParticles2D, CPUParticles2D or custom sprites that follow the bullet's transform with optional offsets. The use of **modern GDExtension features** like virtual methods allow you to override what happens when attachments are spawned/disabled/pooled. These custom methods get called *inside C++* to set up your bullet attachment as necessary. Example - Spawn attachments with visible particles but when they collide you have to disable the emitting and even disable visibility of extra nodes you might've attached. Super flexible and easy to use.

- **Automatic Object Pooling** - MultiMesh instances and attachments are automatically pooled and reused. You can manually populate or free pools, or disable the system to use your own custom logic. You can choose the easy way of using the plugin without any care (because performance is handled for you already) OR you can delve deeper. Example: disable auto pooling, which will allow you to save your bullet instances into an array of multimeshes without fear of problems. Reuse them whenever you want with runtime functions such as `enable_bullet()`/`disable_bullet()`.

- **Dynamic Sparse Set** - The plugin uses custom made data structure that is used internally for precise tracking and looping over ONLY ACTIVE MultiMeshInstances and ONLY THE ACTIVE bullets inside them. This reduces branching and improves performance (a pattern used in ECS engines that could be further improved in future versions of BlastBullets2D).

- **Bullets Custom Data** - Attach a custom resource that the multimesh of bullets carries - used for storing damage, armor damage or anything else custom that should be available during collision.

- **Familiar Signals** - Collisions are tracked with `area_entered` and `body_entered` signals inside the BulletFactory2D node. Combine this with the bullet custom data and you can easily differentiate between types of bullets. You can even detach/attach new bullet attachments or make explosion effects while editing runtime properties inside the function callbacks.

- **Extensive Documentation** - Full in-editor documentation for every function and property, accessible directly within the Godot Inspector and Script Editor.

- **Even Faster Release Builds** - Compiled with Link Time Optimization (LTO) for maximum runtime performance in your exported projects. Debug builds contain the in-engine docs, while release builds are automatically detected and used by Godot for the final release of your game.

- **....AND SO MUCH MORE** - Download the plugin and experiment with it right away! There is also a test_project.zip available where you can check out some of the features and benchmark against a normal Godot Area2D bullet implementation.

<details>
<summary><b>⚠️ When Not To Use BlastBullets2D</b></summary>

While **BlastBullets2D** is highly optimized and feature-rich for top-down 2D games, there are a few limitations you should be aware of:

- ❌ **Y-sorting is not supported**<br>
If your game relies heavily on Y-based depth sorting (common in platformers or isometric games), this plugin may not be a good fit.

- ❌ **Only `RectangleShape2D` is supported for collisions**<br>
Currently, other collision shapes like `CircleShape2D` or `ConvexPolygonShape2D` are not supported.

- ❌ **Only Area2D like behavior**<br>
All bullets act as `Area2D` - they are not `RigidBody2D` and don't support bouncing off of other bullets and so on. Basically you can NOT apply impulse forces. Just view them as `Area2D` bullets.

- ❌**No Save/Load System**<br>
Version 2.0 of BlastBullets2D had a saving/loading system based on custom resources that **got removed in version 3.0.** This is due to the enormous amount of features and changes that got added and the difficulty of keeping everything supported for **every type of game** and **every mix of features**, which proved very hard in practice. The recommended way is for the user to implement this logic depending on the game he's working on - you can fork the repository and begin experimenting. There are currently no plans of this getting implemented again, since people that donated or contacted me privately wanted the advanced features mostly and found the saving/loading unnecessary.

   

In conclusion, **BlastBullets2D is ideal for top-down shooters and arcade-style games**, but may not be suitable for other 2D genres if those specific features are essential to your project.

</details>

---

## How To Install
#### BlastBullets2D targets <b>Godot Engine 4.5 and 4.6</b>. As long as there are no breaking changes to GDExtension in the future, then it should work for all future Godot releases.

1. Go in [Releases](https://github.com/nikoladevelops/godot-blast-bullets-2d/releases) and on the latest release click and download `blastbullets2d.zip`

Note: <b>BlastBullets2D</b> is also available in: [Godot Asset Library](https://godotengine.org/asset-library/asset/2632) and [Itch.io](https://realnikich.itch.io/blastbullets2d).

2. Extract the zip and you will get a single folder `blastbullets2d`
3. Open your <b>Godot</b> game project and paste the folder inside. No matter if you place it inside an `addons` folder or the root of the project, the plugin will still work



The compiled plugin files have been loaded and you are ready to begin coding!

All functions and properties have been documented <b>INSIDE THE EDITOR</b>, so it will be extremely easy for you to take full advantage of all features.

If you want to benchmark/compare performance of `BlastBullets2D` bullets to `Area2D` bullets you can download the second zip file `test_project.zip` that contains the test project showcased in the gif and in the videos on my [Youtube Channel](https://www.youtube.com/@realnikich). I will post some tutorials there.

---
## How To Use
When designing the API, I've ensured that it's as easy as possible for anyone no matter the skill level to use this plugin.

Here is how the basic setup goes:
1. Add a `BulletFactory2D` node to your scene tree. The BulletFactory's job is to spawn bullets and manage plugin related options (debugger, physics interpolations and so on..).
2. The `BulletFactory2D` node has the signals `area_entered`, `body_entered` and `life_time_over`. You should handle them in your script and write custom logic for your game.

Keep a reference to the factory globally, so you can access it in any other script(enemies/player). There's two ways of doing this.

The first way would be to create an [Autoload/Singleton](https://docs.godotengine.org/en/stable/tutorials/scripting/singletons_autoload.html) where you keep the factory as a variable.

The second way would be to create a class with a static variable that keeps the reference of the factory. This approach is the one I'll be using, since you can put your Invetory, UI and other stuff you plan on supporting.

Example:

```
# globals.gd - script for global variables

class_name GLOBALS

static var BULLET_FACTORY:BulletFactory2D

```

Then inside your main level script, you need to save the reference of your factory inside the `BULLET_FACTORY` static variable like so:
```
# main.gd - script of the main scene, where the factory is located

extends Node

func _ready():
	GLOBALS.BULLET_FACTORY = $BulletFactory2D # save a reference to the factory

```

Now the factory can be accessed through any script by doing this
```
# myscript.gd - script where you need to spawn bullets (enemy/player)

func spawn_bullets()->void:
	GLOBALS.BULLET_FACTORY.spawn_directional_bullets(bullets_data)
```

3. Use the `BulletFactory2D`'s functions to spawn bullets in any other script.

#### If you just need normal bullets without extra options:<br>
- <b>`spawn_block_bullets()`</b> - Spawns a multimesh of bullets where the direction is determined by `block_rotation_radians` and the speed by `block_speed`.

- <b>`spawn_directional_bullets()`</b> - Spawns a multimesh of bullets where the direction is determined by the `transforms`'s rotation and each bullet has its own speed data.

#### For advanced features:<br><br>
- <b>`spawn_controllable_directional_bullets`</b> - Same as `spawn_directional_bullets()`, however this method returns the multimesh instance as a result. Save it to a variable and try modifying its properties/ calling functions. This is where all the advanced features are hidden - homing, orbiting, bullet curves, attachments, movement patterns, teleporting, timer related functionality, object pooling options and so on.

#### How to configure `DirectionalBulletsData2D` and `BlockBulletsData2D`?

The spawn functions will either require a `BlockBulletsData2D` or a `DirectionalBulletsData2D`.
It's important that you always check the in-engine documentation of both and also the base class that they inherit from `MultiMeshBulletsData2D`.<br>

The same thing should be said for the `BlockBullets2D`, `DirectionalBullets2D` and `MultiMeshBullets2D` classes, since inside them you will find runtime properties and helper functions. The documentation is always there to help you!


`BlockBulletsData2D` and `DirectionalBulletsData2D` resource classes need to have their `transforms` property set to an array of `Transform2D` - this data determines the global position and rotation of all bullets. The amount of `Transform2D` will also determine the amount of bullets that need to be spawned.

A smart way is to generate these transforms using a bunch of `Marker2D` nodes as children of your player (that is supposed to shoot bullets). This way, as he moves the markers will also move along with him. The only thing you need to do is get all these marker2d's transforms, store them in an array and set it to the bullets data resource class each time you need to spawn bullets. Having a shoot cooldown timer would be nice too.

Only thing is your bullets are invisible and have no collision shape size set..
So how about start playing around with all the properties the data class provides?

Code example:
```
# Returns a partially set up DirectionalBulletsData2D, only thing left to do is set a new value to the .transforms property when the fire cooldown timer times out and you are ready to spawn a new batch of bullets..
func set_up_directional_bullets_data()->DirectionalBulletsData2D:
	var data:DirectionalBulletsData2D = DirectionalBulletsData2D.new()
	data.textures = rocket_textures # Set an array of textures
	# WARNING: Make sure your textures are FACING the Vector2.RIGHT direction when you draw them (basically your bullets should be facing right)
	# If you have to, go through each texture and rotate it manually with an image editor and only then load them all in Godot.

	data.default_change_texture_time = 0.4 # configure default change texture time

	# You can also define wait time for each texture like so as long as the amount of textures matches the amount of values in this array.
	#data.change_texture_times = [
		#0.05,
		#0.03,
		#0.01,
		#0.02,
		#0.01,
		#0.01,
		#0.01,
		#0.08,
		#0.01,
		#0.03
	#]
	
	
	data.all_bullet_speed_data = bullet_speed_data # for the directional bullets use every single bullet speed
	
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([3])

	data.texture_size = Vector2(140,140)
	data.collision_shape_size=Vector2(32,32)
	data.collision_shape_offset=Vector2(0,0)
	data.default_change_texture_time=0.09
	data.max_life_time = 2
	data.all_bullet_rotation_data = bullet_rotation_data
	data.bullets_custom_data = damage_data
	#data.is_life_time_over_signal_enabled = true # If you want to track when the life time is over and receive a signal inside BulletFactory2D
	
	return data
```

#### How do we handle collision and bullet damage?<br>
Notice the ``data.bullets_custom_data = damage_data``. This is a custom resource class instance that you should create. The data it holds can help you differentiate between types of bullets and damage.

Example:


```
class_name DamageData
extends Resource

# The base damage amount
@export var base_damage:int
# Whether the bullet was spawned from the player
@export var is_player_owned:bool
```


Next up go inside the ``BulletFactory2D`` node and register callbacks for the signals ``area_entered``, ``body_entered`` and even ``life_time_over`` if you are interested in it.

Example:

```
# This function is connected to the area_entered signal of the bullet factory. It is executed each time a bullet spawned from the factory hits an Area2D (and again in order for a thing to be hit, ensure the layers are correct!)
func _on_area_entered(hit_target_area: Object, _multimesh_bullets_instance:MultiMeshBullets2D, _bullet_index:int, bullets_custom_data: Resource, _bullet_global_transform: Transform2D) -> void:	
	if hit_target_area is AbstractEnemy:
		var dmg_data:DamageData = bullets_custom_data as DamageData # We know for a fact that we have a DamageData inside our bullets, because that's how we've set them up before spawning them - we can replace it with some other custom resource instead and check for its type here too (we may spawn bullets with different custom data and have additional check logic)
		if dmg_data.is_player_owned == false: # If it wasn't the player who spawned the bullet, then that means an enemy is hitting another enemy - I want the bullet to disappear without it damaging the enemy (No friendly fire :P)
			return
		hit_target_area.take_damage(dmg_data.base_damage) # You can do way more complex damage logic with the rest of the properties inside bullets_custom_data, you can do anything..
		#print("Bullet just collided with an enemy area")
	#else:
		#print("Bullet just collided with an area")

# This function is connected to the body_entered signal of the bullet factory.  It is executed each time a bullet spawned from the factory hits a body (and again in order for a thing to be hit, ensure the layers are correct and you also have enabled the .monitorable property inside bullets data!)
func _on_body_entered(hit_target_body: Object, _multimesh_bullets_instance:MultiMeshBullets2D, _bullet_index:int, bullets_custom_data: Resource, _bullet_global_transform: Transform2D) -> void:
	if hit_target_body is Player:
		var dmg_data:DamageData = bullets_custom_data as DamageData
		hit_target_body.take_damage(dmg_data.base_damage)
		
	#print("Bullet just collided with a body")

# Works only if data.is_life_time_over_signal_enabled set to true
func _on_life_time_over(_multimesh_bullets_instance: MultiMeshBullets2D, _bullet_indexes:Array[int], _bullets_custom_data: Resource, _bullets_global_transforms: Array[Transform2D]) -> void:
	pass
```


#### Accessing advanced features

```
# All this code is inside a function that you call each time you spawn bullets..
# Note the .transforms of the directional_bullets_data class is edited outside this, each time the player shoots..

var dir_bullets:DirectionalBullets2D = BENCHMARK_GLOBALS.FACTORY.spawn_controllable_directional_bullets(directional_bullets_data)

# Homing Functionality
# We have a shared homing deque and a per-bullet homing deque. Depends on what you need

# Settings
dir_bullets.homing_smoothing = 0.0# Set from 0 to 20 or even bigger (but you might have issues with interpolation)
dir_bullets.homing_update_interval = 0.00# Set an update timer - keep it low for smooth updates
dir_bullets.homing_take_control_of_texture_rotation = true
dir_bullets.homing_distance_before_reached = 50 # instead of always going to the center of the texture, this acts like radius offset
dir_bullets.bullet_homing_auto_pop_after_target_reached = false # should it automatically go to the next target in the deque or keep homing even if it had reached the target
dir_bullets.shared_homing_deque_auto_pop_after_target_reached = false

# When target gets reached do this..
dir_bullets.bullet_homing_target_reached.connect(func():
	print("Hey I reached the target..")
)


# Push actual target (choose per-bullet homing or shared or even both).
# Note that shared homing deque takes precedence, until its empty it won't target the per bullet homing deque..

#dir_bullets.shared_homing_deque_push_back_node2d_target(target1) # shared homing
#dir_bullets.bullet_homing_push_back_node2d_target(bullet_index, target1) # per-bullet homing
#dir_bullets.all_bullets_push_back_homing_target(target1) # per-bullet homing helper function with range
# Check out all the docs in the engine where you can push the Mouse or a global position target as the homing target.

# Orbiting
#dir_bullets.bullet_enable_orbiting(...)
#dir_bullets.all_bullets_enable_orbiting(...)

# Bullet Curves Data - Takes precedence over BulletSpeedData2D and BulletRotationData2D
# Note that you should create a BulletCurvesData2D resource in the editor and configure it first
#In FileSystem -> Right Click With Mouse -> New Resource -> Search And Select BulletCurvesData2D. Configure it to your liking.

# Next here in code just do this
#dir_bullets.bullet_set_curves_data(...)
#dir_bullets.all_bullets_set_curves_data(...)

# Movement Patterns
# Note that you should create a Path2D inside your scene and place it/ draw a few points to make a nice pattern.

# Get a reference of it and call these:
#dir_bullets.set_bullet_movement_pattern_from_path(...)
#dir_bullets.all_bullets_set_movement_pattern_from_path(...)

# Bullet attachments - attaching GPU particles and other custom nodes to follow the bullets..

# Create a brand new scene and make sure the type is set to BulletAttachment2D. Very important!
# Place particles or anything you want inside
# Next override the functions ``on_bullet_disable()``, ``on_bullet_enable()``, ``on_bullet_spawn()``,``on_spawn_in_pool()`` and ``_ready()`` and implement custom logic to your liking.
#(Check out the test_project.zip for examples)

# Load the BulletAttachment2D a PackedScene, then do this:
#dir_bullets.bullet_set_attachment(...)
#dir_bullets.all_bullets_set_attachment(...)


# Attach timer related functions
# Imagine wanting to change the movement pattern at a specific time, or you want your bullets to home towards another target or you want to switch textures and so on..

# To edit runtime properties safely at a specific time, use this function:
#dir_bullets.multimesh_attach_time_based_function(0.5, func():
	# custom code goes here
	# change orbiting radius, do anything basically..
#)

```


This is pretty much everything you need to know to get started. Practice with the plugin by looking at the in-engine docs!

You can also download the `test_project.zip` from the latest release and get a feel for it by modifying the `player_data_node.gd` script.

For smooth bullets no matter the refresh rate enable physics interpolation in your project settings as well as
tick the checkbox inside the inspector in BulletFactory2D. That's all, enjoy the plugin!


## How To Compile

Fork the repository, then go inside the GitHub Actions tab, and run the workflow for `full_plugin_compilation`, instead of `debug`. This easy process is due to using [godot-plus-plus template](https://github.com/nikoladevelops/godot-plus-plus) - allows for cross platform easy workflow when making Godot C++ GDExtension plugins.

[GDExtension C++ Tutorial](https://www.youtube.com/watch?v=I79u5KNl34o&t=1s)

## Support
If you wish to support me you can do so here - https://ko-fi.com/realnikich or https://patreon.com/realnikich

If you find this plugin useful:
- <b>Leave a Star on the repository</b>
- Expect tutorials on my YouTube channel - https://www.youtube.com/@realnikich
- [Follow me on X (Twitter)](https://x.com/realNikich)
- [Follow me on Bluesky](https://bsky.app/profile/realnikich.bsky.social)

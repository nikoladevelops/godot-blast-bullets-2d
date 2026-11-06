class_name AttachmentProbe2D
extends BulletAttachment2D
## Probe fixture for the pooling attachment suites.
## Counts every virtual callback so tests assert exact lifecycle sequences:
## fresh spawn (on_bullet_spawn), wake (on_bullet_enable), kill
## (on_bullet_disable), pre-population (on_spawn_in_pool). Counters live on the
## instance and survive pooling, so reuse sequences accumulate and stay
## assertable via volley.bullet_get_attachment(i).

var spawn_calls := 0
var enable_calls := 0
var disable_calls := 0
var in_pool_calls := 0

func on_bullet_spawn() -> void:
	spawn_calls += 1

func on_bullet_enable() -> void:
	enable_calls += 1

func on_bullet_disable() -> void:
	disable_calls += 1

func on_spawn_in_pool() -> void:
	in_pool_calls += 1

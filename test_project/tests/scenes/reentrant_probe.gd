class_name ReentrantProbe2D
extends BulletAttachment2D
## Fixture for the disable-reentrancy suite: its on_bullet_disable calls back
## into the volley, disabling a second slot from inside the outer disable.
## Without the disable_bullet latch this nested call proceeds and the volley
## pools itself twice; with the latch it is rejected and state stays exact.
## Target volley/victim cross via statics (set before the outer disable).

static var volley: BulletVolley2D = null
static var victim := -1
static var attempts := 0

static func reset_state() -> void:
	volley = null
	victim = -1
	attempts = 0

func on_bullet_disable() -> void:
	attempts += 1
	if volley != null and victim >= 0:
		volley.disable_bullet(victim)

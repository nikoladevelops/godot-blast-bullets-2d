class_name AttachmentHookProbe2D
extends BulletAttachment2D
## Runs a test-provided hook from inside the attachment callbacks named by
## `hook_on` ("spawn", "enable", "disable"; several allowed): once, or on
## every call with `hook_repeat`. The callback-free matrix frees volleys and
## factories from there.

static var hook: Callable
static var hook_on: Array = []
static var hook_repeat := false
static var fired := 0


static func clear_hook() -> void:
	hook = Callable()
	hook_on = []
	hook_repeat = false
	fired = 0


static func _run(callback: String) -> void:
	if hook.is_valid() and callback in hook_on:
		var h := hook
		if not hook_repeat:
			hook = Callable()
		fired += 1
		h.call()


func on_bullet_spawn() -> void:
	_run("spawn")


func on_bullet_enable() -> void:
	_run("enable")


func on_bullet_disable() -> void:
	_run("disable")

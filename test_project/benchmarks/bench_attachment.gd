extends BulletAttachment2D
## Minimal attachment for benchmarks: a tiny polygon so the node has render
## work comparable to a real sprite attachment (no script callbacks).
func _ready() -> void:
	var p := Polygon2D.new()
	p.polygon = PackedVector2Array([Vector2(-2, -2), Vector2(2, -2), Vector2(2, 2), Vector2(-2, 2)])
	add_child(p)

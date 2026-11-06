extends CanvasLayer
## A space backdrop behind everything: hands the shader where the camera looks and how far it
## zooms, so the nebula and the stars scroll at their own parallax with any Camera2D (or none).

@onready var _rect: ColorRect = $Backdrop


func _process(_delta: float) -> void:
	var view := get_viewport()
	var canvas := view.get_canvas_transform()
	var size := view.get_visible_rect().size
	var zoom := canvas.get_scale().x
	var mat := _rect.material as ShaderMaterial
	mat.set_shader_parameter(&"rect_size", size)
	mat.set_shader_parameter(&"camera_zoom", zoom)
	mat.set_shader_parameter(&"camera_center", canvas.affine_inverse() * (size * 0.5))

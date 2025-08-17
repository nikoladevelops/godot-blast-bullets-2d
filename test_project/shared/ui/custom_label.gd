class_name CustomLabel
extends HBoxContainer

@export var default_text:String = "TextGoesHere: "
@export var default_value:String = "ValueGoesHere"

@onready var text_label:Label = $TextLabel
@onready var value_label:Label = $ValueLabel
		
func _ready()->void:
	text_label.text = default_text
	value_label.text = default_value
	
func update_text(new_text:String)->void:
	text_label.text = new_text

func update_value(new_value:String)->void:
	value_label.text = new_value

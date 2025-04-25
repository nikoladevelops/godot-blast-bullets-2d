class_name SwitchButton
extends Button

# A collection of text that the button switches between when pressed
@export var text_options:Array[String]

# The currently selected text option index from text_options array
@export var current_selected_option_index:int = 0

# Emitted when the SwitchButton gets pressed. As an argument it gives you the new text that the button has as selected_option as well as the index that the option resides in as selected_option_index 
signal switch_btn_pressed(selected_option:String, selected_option_index:int)

func _ready() -> void:
	var arr_size:int = text_options.size()
	if arr_size == 0:
		printerr("Your SwitchButton has no text options. This is a bug please add some")
		return
		
	if current_selected_option_index >= text_options.size() || current_selected_option_index < 0:
		current_selected_option_index = 0
		printerr("Your SwitchButton had an invalid index, it was reset to 0. This is a bug, fix it")
	
	text = text_options[current_selected_option_index]
	pressed.connect(_emit_switch_btn_pressed)
	
func _emit_switch_btn_pressed()->void:
	var arr_size:int = text_options.size()
	if arr_size == 0:
		printerr("Your SwitchButton has no text options. This is a bug please add some")
		return
		
	current_selected_option_index = current_selected_option_index + 1
	
	if current_selected_option_index >= text_options.size():
		current_selected_option_index = 0
	
	var new_btn_text:String = text_options[current_selected_option_index]
	text = new_btn_text

	emit_signal("switch_btn_pressed", new_btn_text, current_selected_option_index)
	
func switch_to_option_index(option_index)->void:
	current_selected_option_index = option_index
	
	if current_selected_option_index >= text_options.size() || current_selected_option_index < 0:
		current_selected_option_index = 0
		printerr("Your SwitchButton had an invalid index, it was reset to 0. This is a bug, fix it")
		
	text = text_options[current_selected_option_index]

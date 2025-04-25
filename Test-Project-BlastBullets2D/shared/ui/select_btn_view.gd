class_name SelectBtnView
extends Control

# All buttons that are available to choose from
@export var all_btns:Array[Button]
# The currently selected button index (0 means the first button)
@export var selected_btn_index:int
# The theme that is applied to the currently selected btn
@export var selected_btn_theme:Theme = preload("res://shared/ui/btn_enabled_theme.tres")
# The theme that is applied to the rest of the buttons
@export var disabled_btn_theme:Theme = preload("res://shared/ui/btn_disabled_theme.tres")

# Retrieve the currently selected button
var get_selected_btn:Button:
	get: return all_btns[selected_btn_index]

# Emitted when a different button is selected
signal new_btn_selected(new_selected_btn:Button)

func _ready()->void:
	if all_btns == null:
		print("You have not set any buttons for the select btn view, this might cause errors!")
		return
	
	# For each button
	for i in range(all_btns.size()):
		# Connect a function that gets executed when one of the buttons is pressed
		var curr_btn:Button = all_btns[i]
		if curr_btn == null:
			printerr("Your UI SelectBtnView has a null instance inside. You probably meant to pass an actual instance but forgot")
			continue
		
		curr_btn.pressed.connect(_on_btn_pressed.bind(i)) 
		
		# Set the appropriate theme for each button
		if i == selected_btn_index:
			curr_btn.theme = selected_btn_theme
			continue;
		curr_btn.theme = disabled_btn_theme

func _on_btn_pressed(btn_index:int)->void:
	# It does nothing if the same exact selected button gets pressed again
	if btn_index == selected_btn_index:
		return
	
	# Mark the last selected btn as disabled
	all_btns[selected_btn_index].theme = disabled_btn_theme
	
	# Mark the new btn as selected
	all_btns[btn_index].theme = selected_btn_theme
	selected_btn_index = btn_index
	
	# Tell the outside world that a new btn has been selected and pass it as an argument
	emit_signal("new_btn_selected", all_btns[btn_index])

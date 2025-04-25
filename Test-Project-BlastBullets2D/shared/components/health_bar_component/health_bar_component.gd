class_name HealthBarComponent
extends ProgressBar

@onready var value_label:Label = $ValueLabel

var health:float:
	get:
		return value
	set(new_health):
		value = new_health
		update_value_label()

# Sets up the health bar component in valid state
func set_up(max_health:float, current_health:float = max_health, min_health:float=0)->void:
	max_value = max_health
	health = current_health
	min_value = min_health
	
# Takes damage
func take_damage(dmg_amount:int)->void:
	health-=dmg_amount
	visible=true # Ensure it's visible when taking damage

# Updates the value label to display the newest health value
func update_value_label()->void:
	value_label.text = str(value) + '/' + str(max_value)

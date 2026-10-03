extends VBoxContainer

@onready var update_debug_data_timer:Timer = $UpdateDebugDataTimer

@onready var active_volleys_custom_label:CustomLabel = $ActiveVolleysCustomLabel
@onready var active_attachments_custom_label:CustomLabel = $ActiveBulletAttachmentsCustomLabel

@onready var pooled_volleys_custom_label:CustomLabel = $PooledVolleysCustomLabel
@onready var pooled_attachments_custom_label:CustomLabel = $PooledBulletAttachmentsCustomLabel

@onready var volley_pool_info_custom_label:CustomLabel = $VolleyPoolInfoCustomLabel
@onready var attachments_pool_info_custom_label:CustomLabel = $AttachmentsPoolInfoCustomLabel

func _ready():
	pass
	
func _on_update_debug_data_timer_timeout() -> void:
	# Currently active in scene tree
	active_volleys_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_active_bullets_amount())
	)
	
	active_attachments_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_active_attachments_amount())
	)
	
	# Object Pool related
	pooled_volleys_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_bullets_pool_amount())
	)
	
	pooled_attachments_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_attachments_pool_amount())
	)
	
	# What each object pool actually contains
	volley_pool_info_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_bullets_pool_info())
	)
	
	attachments_pool_info_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_attachments_pool_info())
	)

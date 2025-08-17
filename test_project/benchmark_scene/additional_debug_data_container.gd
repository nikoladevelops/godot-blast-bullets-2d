extends VBoxContainer

@onready var update_debug_data_timer:Timer = $UpdateDebugDataTimer

@onready var active_directional_multi_meshes_custom_label:CustomLabel = $ActiveDirectionalMultiMeshesCustomLabel
@onready var active_block_multi_meshes_custom_label:CustomLabel = $ActiveBlockMultiMeshesCustomLabel
@onready var active_attachments_custom_label:CustomLabel = $ActiveBulletAttachmentsCustomLabel

@onready var pooled_directional_multi_meshes_custom_label:CustomLabel = $PooledDirectionalMultiMeshesCustomLabel
@onready var pooled_block_multi_meshes_custom_label:CustomLabel = $PooledBlockMultiMeshesCustomLabel
@onready var pooled_attachments_custom_label:CustomLabel = $PooledBulletAttachmentsCustomLabel

@onready var directional_pool_info_custom_label:CustomLabel = $DirectionalPoolInfoCustomLabel
@onready var block_pool_info_custom_label:CustomLabel = $BlockPoolInfoCustomLabel
@onready var attachments_pool_info_custom_label:CustomLabel = $AttachmentsPoolInfoCustomLabel

func _ready():
	pass
	
func _on_update_debug_data_timer_timeout() -> void:
	# Currently active in scene tree
	active_directional_multi_meshes_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_active_bullets_amount(BulletFactory2D.DIRECTIONAL_BULLETS))
	)
	
	active_block_multi_meshes_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_active_bullets_amount(BulletFactory2D.BLOCK_BULLETS))
	)
	
	active_attachments_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_active_attachments_amount())
	)
	
	# Object Pool related
	pooled_directional_multi_meshes_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_bullets_pool_amount(BulletFactory2D.DIRECTIONAL_BULLETS))
	)
	
	pooled_block_multi_meshes_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_bullets_pool_amount(BulletFactory2D.BLOCK_BULLETS))
	)
	
	pooled_attachments_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_attachments_pool_amount())
	)
	
	# What each object pool actually contains
	directional_pool_info_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_bullets_pool_info(BulletFactory2D.DIRECTIONAL_BULLETS))
	)
	
	block_pool_info_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_bullets_pool_info(BulletFactory2D.BLOCK_BULLETS))
	)
	
	attachments_pool_info_custom_label.update_value(
		str(BENCHMARK_GLOBALS.FACTORY.debug_get_attachments_pool_info())
	)

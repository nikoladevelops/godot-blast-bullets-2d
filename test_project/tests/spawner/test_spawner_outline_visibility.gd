extends BlastTest
## helper_outline_distribution only steers corner-anchored apportionment, so
## the inspector hides it for smooth loops and shows it for corner shapes.

const SMOOTH := [
	BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE,
	BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE,
	BulletSpawner2D.PATTERN_FROM_HELPER_HEART,
	BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER,
	BulletSpawner2D.PATTERN_FROM_HELPER_ROSE,
	BulletSpawner2D.PATTERN_FROM_HELPER_LISSAJOUS,
]
const CORNER := [
	BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE,
	BulletSpawner2D.PATTERN_FROM_HELPER_SQUARE,
	BulletSpawner2D.PATTERN_FROM_HELPER_POLYGON,
	BulletSpawner2D.PATTERN_FROM_HELPER_TRIANGLE,
	BulletSpawner2D.PATTERN_FROM_HELPER_TRAPEZOID,
	BulletSpawner2D.PATTERN_FROM_HELPER_DIAMOND,
	BulletSpawner2D.PATTERN_FROM_HELPER_STAR,
]


func _visible_for(src: int) -> bool:
	var sp := make_spawner()
	sp.helper_outline_placement = 0 # ON_OUTLINE
	sp.pattern_source = src
	return is_editor_visible(sp, &"helper_outline_distribution")


func test_hidden_for_smooth_loops() -> void:
	for src in SMOOTH:
		assert_false(_visible_for(src), "distribution hidden for smooth source %d" % src)


func test_shown_for_corner_shapes() -> void:
	for src in CORNER:
		assert_true(_visible_for(src), "distribution shown for corner source %d" % src)

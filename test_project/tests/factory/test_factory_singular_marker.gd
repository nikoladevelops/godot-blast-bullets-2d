extends BlastTest
## Singular markers: generators that invert the marker must reject singular
## markers loudly; non-inverting generators degrade to finite zero-size.
##
## A (0, 1)-scale marker passes every finite check but has determinant 0, so
## affine_inverse() (outline layout, Path2D linker) is garbage.

var singular := Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2(100, 50))


func test_singular_marker_is_finite_but_singular() -> void:
	assert_true(singular.is_finite(), "passes finite-only guards")
	assert_eq(singular.determinant(), 0.0)


func test_ring_rejects_singular_marker() -> void:
	var ring = BulletFactory2D.helper_generate_transforms_ring(12, singular, 60.0, 0.0, TAU, true, false, true)
	assert_eq(ring.size(), 0, "ring volley empty on singular marker")
	expect_any_error()


func test_circle_rejects_singular_marker() -> void:
	var circ = BulletFactory2D.helper_generate_transforms_circle(12, singular, 60.0, true, 0.0)
	assert_eq(circ.size(), 0, "circle volley empty on singular marker")
	expect_any_error()


func test_grid_degrades_finite_on_singular_marker() -> void:
	var grid = BulletFactory2D.helper_generate_transforms_grid(6, singular, 3, 4, 32.0, 32.0, true, false, 0.0, 0)
	assert_eq(grid.size(), 6, "grid still emits")
	assert_true(H.finite_volley(grid), "grid slots finite (graceful zero-size)")


func test_sane_marker_unaffected() -> void:
	var ring_ok = BulletFactory2D.helper_generate_transforms_ring(12, Transform2D(0.3, Vector2(100, 50)), 60.0, 0.0, TAU, true, false, true)
	assert_eq(ring_ok.size(), 12)
	assert_true(H.finite_volley(ring_ok), "ring slots finite")

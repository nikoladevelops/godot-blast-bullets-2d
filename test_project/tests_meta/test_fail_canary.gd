extends GutTest
## Runner self-test only: one failing assert and one unexpected engine error,
## proving failures are detected. Never part of the normal run.

func test_fails_on_purpose() -> void:
	assert_eq(1, 2, "canary: deliberate failure")

func test_unexpected_error_fails() -> void:
	push_error("canary: unexpected error")
	assert_true(true)

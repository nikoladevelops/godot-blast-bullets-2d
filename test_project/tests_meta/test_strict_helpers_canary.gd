extends BlastTest
## Runner self-test only: every precise error helper MUST fail when the
## recorded errors do not match. Each test below is expected to FAIL; the
## runner's --self-test counts them. Never part of the normal run.


func test_sequence_missing_error_fails() -> void:
	expect_error_sequence(["this error never happened"])


func test_sequence_extra_error_fails() -> void:
	push_error("canary: first")
	push_error("canary: second")
	expect_error_sequence(["canary: first"])


func test_sequence_wrong_order_fails() -> void:
	push_error("canary: b")
	push_error("canary: a")
	expect_error_sequence(["canary: a", "canary: b"])


func test_containing_is_exact_by_default() -> void:
	push_error("canary: dup")
	push_error("canary: dup")
	expect_errors_containing("canary: dup", 1)


func test_no_errors_checkpoint_fails() -> void:
	push_error("canary: stray")
	expect_no_errors()

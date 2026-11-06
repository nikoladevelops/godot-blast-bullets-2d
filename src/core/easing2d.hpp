#pragma once

#include <godot_cpp/core/math.hpp>

namespace BlastBullets2D {
using namespace godot;

// Godot's Tween easing curves (Tween.TransitionType x Tween.EaseType) as
// plain functions: ease(t) for t in [0, 1] -> progress (may overshoot for
// BACK / ELASTIC / SPRING). No Variant, no engine call, so a moving spawner
// can evaluate it every frame for free. Same equations and constants as
// Godot's scene/animation/easing_equations.h; parity with
// Tween.interpolate_value is pinned by tests/spawner/test_spawner_easing.gd
// for every transition x ease at many samples.
namespace Easing2D {

enum Transition {
	TRANS_LINEAR = 0,
	TRANS_SINE,
	TRANS_QUINT,
	TRANS_QUART,
	TRANS_QUAD,
	TRANS_EXPO,
	TRANS_ELASTIC,
	TRANS_CUBIC,
	TRANS_CIRC,
	TRANS_BOUNCE,
	TRANS_BACK,
	TRANS_SPRING,
	TRANS_COUNT
};

enum Ease {
	EASE_IN = 0,
	EASE_OUT,
	EASE_IN_OUT,
	EASE_OUT_IN,
	EASE_COUNT
};

// All functions use Godot's (t, b, c, d) form: elapsed t, start b, change c,
// duration d. We always call them with b = 0, c = 1, d = 1.

namespace linear {
inline double in(double t, double b, double c, double d) { return c * t / d + b; }
} // namespace linear

namespace sine {
inline double in(double t, double b, double c, double d) { return -c * Math::cos(t / d * (Math::PI / 2)) + c + b; }
inline double out(double t, double b, double c, double d) { return c * Math::sin(t / d * (Math::PI / 2)) + b; }
inline double in_out(double t, double b, double c, double d) { return -c / 2 * (Math::cos(Math::PI * t / d) - 1) + b; }
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace sine

namespace quint {
inline double in(double t, double b, double c, double d) { return c * Math::pow(t / d, 5) + b; }
inline double out(double t, double b, double c, double d) { return c * (Math::pow(t / d - 1, 5) + 1) + b; }
inline double in_out(double t, double b, double c, double d) {
	t = t / d * 2;
	if (t < 1) {
		return c / 2 * Math::pow(t, 5) + b;
	}
	return c / 2 * (Math::pow(t - 2, 5) + 2) + b;
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace quint

namespace quart {
inline double in(double t, double b, double c, double d) { return c * Math::pow(t / d, 4) + b; }
inline double out(double t, double b, double c, double d) { return -c * (Math::pow(t / d - 1, 4) - 1) + b; }
inline double in_out(double t, double b, double c, double d) {
	t = t / d * 2;
	if (t < 1) {
		return c / 2 * Math::pow(t, 4) + b;
	}
	return -c / 2 * (Math::pow(t - 2, 4) - 2) + b;
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace quart

namespace quad {
inline double in(double t, double b, double c, double d) { return c * Math::pow(t / d, 2) + b; }
inline double out(double t, double b, double c, double d) {
	t /= d;
	return -c * t * (t - 2) + b;
}
inline double in_out(double t, double b, double c, double d) {
	t = t / d * 2;
	if (t < 1) {
		return c / 2 * Math::pow(t, 2) + b;
	}
	return -c / 2 * ((t - 1) * (t - 3) - 1) + b;
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace quad

namespace expo {
inline double in(double t, double b, double c, double d) {
	if (t == 0) {
		return b;
	}
	return c * Math::pow(2, 10 * (t / d - 1)) + b - c * 0.001;
}
inline double out(double t, double b, double c, double d) {
	if (t == d) {
		return b + c;
	}
	return c * 1.001 * (-Math::pow(2, -10 * t / d) + 1) + b;
}
inline double in_out(double t, double b, double c, double d) {
	if (t == 0) {
		return b;
	}
	if (t == d) {
		return b + c;
	}
	t = t / d * 2;
	if (t < 1) {
		return c / 2 * Math::pow(2, 10 * (t - 1)) + b - c * 0.0005;
	}
	return c / 2 * 1.0005 * (-Math::pow(2, -10 * (t - 1)) + 2) + b;
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace expo

namespace elastic {
inline double in(double t, double b, double c, double d) {
	if (t == 0) {
		return b;
	}
	t /= d;
	if (t == 1) {
		return b + c;
	}
	t -= 1;
	const double p = d * 0.3;
	const double a = c * Math::pow(2, 10 * t);
	const double s = p / 4;
	return -(a * Math::sin((t * d - s) * (2 * Math::PI) / p)) + b;
}
inline double out(double t, double b, double c, double d) {
	if (t == 0) {
		return b;
	}
	t /= d;
	if (t == 1) {
		return b + c;
	}
	const double p = d * 0.3;
	const double s = p / 4;
	return (c * Math::pow(2, -10 * t) * Math::sin((t * d - s) * (2 * Math::PI) / p) + c + b);
}
inline double in_out(double t, double b, double c, double d) {
	if (t == 0) {
		return b;
	}
	if ((t /= d / 2) == 2) {
		return b + c;
	}
	const double p = d * (0.3 * 1.5);
	const double a = c;
	const double s = p / 4;
	if (t < 1) {
		t -= 1;
		const double post = a * Math::pow(2, 10 * t);
		return -0.5 * (post * Math::sin((t * d - s) * (2 * Math::PI) / p)) + b;
	}
	t -= 1;
	const double post = a * Math::pow(2, -10 * t);
	return post * Math::sin((t * d - s) * (2 * Math::PI) / p) * 0.5 + c + b;
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace elastic

namespace cubic {
inline double in(double t, double b, double c, double d) {
	t /= d;
	return c * t * t * t + b;
}
inline double out(double t, double b, double c, double d) {
	t = t / d - 1;
	return c * (t * t * t + 1) + b;
}
inline double in_out(double t, double b, double c, double d) {
	t /= d / 2;
	if (t < 1) {
		return c / 2 * t * t * t + b;
	}
	t -= 2;
	return c / 2 * (t * t * t + 2) + b;
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace cubic

namespace circ {
inline double in(double t, double b, double c, double d) {
	t /= d;
	return -c * (Math::sqrt(1 - t * t) - 1) + b;
}
inline double out(double t, double b, double c, double d) {
	t = t / d - 1;
	return c * Math::sqrt(1 - t * t) + b;
}
inline double in_out(double t, double b, double c, double d) {
	t /= d / 2;
	if (t < 1) {
		return -c / 2 * (Math::sqrt(1 - t * t) - 1) + b;
	}
	t -= 2;
	return c / 2 * (Math::sqrt(1 - t * t) + 1) + b;
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace circ

namespace bounce {
inline double out(double t, double b, double c, double d) {
	t /= d;
	if (t < (1 / 2.75)) {
		return c * (7.5625 * t * t) + b;
	}
	if (t < (2 / 2.75)) {
		t -= 1.5 / 2.75;
		return c * (7.5625 * t * t + 0.75) + b;
	}
	if (t < (2.5 / 2.75)) {
		t -= 2.25 / 2.75;
		return c * (7.5625 * t * t + 0.9375) + b;
	}
	t -= 2.625 / 2.75;
	return c * (7.5625 * t * t + 0.984375) + b;
}
inline double in(double t, double b, double c, double d) { return c - out(d - t, 0, c, d) + b; }
inline double in_out(double t, double b, double c, double d) {
	if (t < d / 2) {
		return in(t * 2, b, c / 2, d);
	}
	return out(t * 2 - d, b + c / 2, c / 2, d);
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace bounce

namespace back {
inline double in(double t, double b, double c, double d) {
	const double s = 1.70158;
	t /= d;
	return c * t * t * ((s + 1) * t - s) + b;
}
inline double out(double t, double b, double c, double d) {
	const double s = 1.70158;
	t = t / d - 1;
	return c * (t * t * ((s + 1) * t + s) + 1) + b;
}
inline double in_out(double t, double b, double c, double d) {
	const double s = 1.70158 * 1.525;
	t /= d / 2;
	if (t < 1) {
		return c / 2 * (t * t * ((s + 1) * t - s)) + b;
	}
	t -= 2;
	return c / 2 * (t * t * ((s + 1) * t + s) + 2) + b;
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace back

namespace spring {
inline double out(double t, double b, double c, double d) {
	t /= d;
	const double s = 1.0 - t;
	t = (Math::sin(t * Math::PI * (0.2 + 2.5 * t * t * t)) * Math::pow(s, 2.2) + t) * (1.0 + (1.2 * s));
	return c * t + b;
}
inline double in(double t, double b, double c, double d) { return c - out(d - t, 0, c, d) + b; }
inline double in_out(double t, double b, double c, double d) {
	if (t < d / 2) {
		return in(t * 2, b, c / 2, d);
	}
	return out(t * 2 - d, b + c / 2, c / 2, d);
}
inline double out_in(double t, double b, double c, double d) { return (t < d / 2) ? out(t * 2, b, c / 2, d) : in(t * 2 - d, b + c / 2, c / 2, d); }
} // namespace spring

// Progress for normalized time t in [0, 1] (clamped). Invalid enums fall
// back to linear.
inline double ease(double t, int transition, int ease_type) {
	if (!(t > 0.0)) {
		t = 0.0;
	} else if (t > 1.0) {
		t = 1.0;
	}
	typedef double (*Fn)(double, double, double, double);
	static const Fn table[TRANS_COUNT][EASE_COUNT] = {
		{ linear::in, linear::in, linear::in, linear::in },
		{ sine::in, sine::out, sine::in_out, sine::out_in },
		{ quint::in, quint::out, quint::in_out, quint::out_in },
		{ quart::in, quart::out, quart::in_out, quart::out_in },
		{ quad::in, quad::out, quad::in_out, quad::out_in },
		{ expo::in, expo::out, expo::in_out, expo::out_in },
		{ elastic::in, elastic::out, elastic::in_out, elastic::out_in },
		{ cubic::in, cubic::out, cubic::in_out, cubic::out_in },
		{ circ::in, circ::out, circ::in_out, circ::out_in },
		{ bounce::in, bounce::out, bounce::in_out, bounce::out_in },
		{ back::in, back::out, back::in_out, back::out_in },
		{ spring::in, spring::out, spring::in_out, spring::out_in },
	};
	if (transition < 0 || transition >= TRANS_COUNT || ease_type < 0 || ease_type >= EASE_COUNT) {
		return t;
	}
	return table[transition][ease_type](t, 0.0, 1.0, 1.0);
}

} // namespace Easing2D
} // namespace BlastBullets2D

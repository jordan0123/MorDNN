#pragma once

#include "SDK.h"
#include <string>

using namespace SDK;

// SDK FVECTORS *********************
inline FVector fvector(float x, float y, float z) {
	FVector vec;
	vec.X = x;
	vec.Y = y;
	vec.Z = z;

	return vec;
}

inline FVector operator+(const FVector& a, const FVector& b) {
	FVector result;

	result.X = a.X + b.X;
	result.Y = a.Y + b.Y;
	result.Z = a.Z + b.Z;

	return result;
}

inline FVector operator-(const FVector& a, const FVector& b) {
	FVector result;

	result.X = a.X - b.X;
	result.Y = a.Y - b.Y;
	result.Z = a.Z - b.Z;

	return result;
}

inline FVector operator*(const FVector& a, const float scalar) {
	FVector result;

	result.X = a.X * scalar;
	result.Y = a.Y * scalar;
	result.Z = a.Z * scalar;

	return result;
}

inline float dot(const FVector& a, const FVector& b) {
	return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

inline float len2(const FVector& a) {
	return a.X * a.X + a.Y * a.Y + a.Z * a.Z;
}

inline float len(const FVector& a) {
	return sqrt(a.X * a.X + a.Y * a.Y + a.Z * a.Z);
}

inline float dist(const FVector& a, const FVector& b) {
	return len(a - b);
}

std::string vecToString(const FVector& v) {
	return std::to_string(v.X) + ", " + std::to_string(v.Y) + ", " + std::to_string(v.Z);
}

// transforms world vectors into local plane rotated by my forward vector
FVector toLocal_Forward(const FVector& world_pos, const FVector& reference_frame_pos, const FVector& forward) {
	float yaw = -atan2(forward.Y, forward.X);
	FVector rel_pos = world_pos - reference_frame_pos;
	return fvector(cos(yaw) * rel_pos.X - sin(yaw) * rel_pos.Y, sin(yaw) * rel_pos.X + cos(yaw) * rel_pos.Y, rel_pos.Z);
}

// transforms world vectors into local plane rotated by the relative yaw between me and the target
FVector toLocal_MeToEnemy(const FVector& my_pos, const FVector& enemy_pos, const FVector& world_pos) {
	float yaw = -atan2(enemy_pos.Y - my_pos.Y, enemy_pos.X - my_pos.X);
	FVector rel_pos = world_pos - my_pos;
	return fvector(cos(yaw) * rel_pos.X - sin(yaw) * rel_pos.Y, sin(yaw) * rel_pos.X + cos(yaw) * rel_pos.Y, rel_pos.Z);
}

// other math *********************
inline float normalizeAngle(float angle) {
	float newAngle = angle;
	while (newAngle <= -180) newAngle += 360;
	while (newAngle > 180) newAngle -= 360;
	return newAngle;
}

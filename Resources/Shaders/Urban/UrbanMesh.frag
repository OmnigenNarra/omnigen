#version 430
const vec3 lightDirection = vec3(0.57735026919f, -0.57735026919f, 0.57735026919f);
const float ambient = 0.2f;

uniform sampler2D diffuseTextures[4];
uniform sampler2D normalTextures[4];
uniform int objectID;

in vec3 pNormal;
in vec3 pSurfaceNormal;
flat in uint pMatId;
in flat uint instanceID;
in vec2 pUV;

layout (location = 0) out vec4 fragColor;
layout (location = 1) out vec4 outData;

vec4 makeRotationQuaternion(vec3 start, vec3 dest)
{
	float cosTheta = dot(start, dest);

    // special case: opposite vectors
    if (cosTheta < -1 + 0.01f)
        return vec4(0,0,0,0); // special-case'd later

	vec3 rotationAxis = cross(start, dest);

	float s = sqrt( (1+cosTheta)*2 );
	float invs = 1 / s;

	return vec4(
		rotationAxis.x * invs,
		rotationAxis.y * invs,
		rotationAxis.z * invs,
        s * 0.5f
	);
}

vec3 rotateVector(in vec3 v, in vec4 q)
{
    // special case: opposite vectors
    if (q == vec4(0,0,0,0))
        return -v;

    // Extract the vector part of the quaternion
    vec3 u = vec3(q.x, q.y, q.z);

    // Extract the scalar part of the quaternion
    float s = q.w;

    // Do the math
    return vec3(2.0f * dot(u, v) * u
          + (s*s - dot(u, u)) * v
          + 2.0f * s * cross(u, v));
}

vec4 slerp(in vec4 q1, in vec4 q2, float t)
{
    // Handle the easy cases first.
    if (t <= 0.0f)
        return q1;
    else if (t >= 1.0f)
        return q2;
    // Determine the angle between the two quaternions.
    vec4 q2b = q2;
    float Dot = dot(q1, q2);
    if (Dot < 0.0f) {
        q2b = -q2b;
        Dot = -Dot;
    }
    // Get the scale factors.  If they are too small,
    // then revert to simple linear interpolation.
    float factor1 = 1.0f - t;
    float factor2 = t;
    if ((1.0f - Dot) > 0.0000001) {
        float angle = acos(Dot);
        float sinOfAngle = sin(angle);
        if (sinOfAngle > 0.0000001) {
            factor1 = sin((1.0f - t) * angle) / sinOfAngle;
            factor2 = sin(t * angle) / sinOfAngle;
        }
    }

    // Construct the result quaternion.
    return q1 * factor1 + q2b * factor2;
}

void main()
{
    vec4 texColor = texture(diffuseTextures[pMatId], pUV);
    if (texColor.a == 0)
        discard;

    vec3 N = normalize(pNormal);
    vec4 normalBaseQuat = makeRotationQuaternion(vec3(0,0,1), N);

    vec3 texNormal = texture(normalTextures[pMatId], pUV).xyz;
    texNormal = rotateVector(texNormal, normalBaseQuat);
    vec4 normal2SurfaceQuat = makeRotationQuaternion(texNormal, pSurfaceNormal);
    vec4 intepolatedQuat = slerp(vec4(0,0,0,1), normal2SurfaceQuat, 0.5f);
    texNormal = normalize(rotateVector(texNormal, intepolatedQuat));

    // Lambert
    float factor = dot(lightDirection, -texNormal);
    factor = clamp(factor, 0.0f, 1.0f - ambient) + ambient;
    fragColor = texColor * factor;
    fragColor.w = 1;

    outData = vec4(objectID, instanceID, gl_PrimitiveID, 1);
}
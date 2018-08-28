#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <cassert>

#include "math.h"
#include "taskflow.hpp"

struct image
{
	struct pixel
	{
		uint8_t b, g, r;
	};

	static constexpr pixel white = { 255, 255, 255 };
	static constexpr pixel black = { 0, 0, 0 };

	image(int width, int height) : 
		width(width), 
		height(height), 
		pitch(width * sizeof(pixel)),
		data(width * height, white)
	{
	}

	const int width;
	const int height;
	const int pitch;

	std::vector<pixel> data;
};

math::vec<3> linear_to_srgb(const math::vec<3>& color)
{
	math::vec<3> result;
	for (int i = 0; i < 3; ++i)
	{
		result[i] = 1.055f * std::pow(color[i], 0.416666667f) - 0.055f;
	}
	return result;
}

struct ray
{
	ray()
	{
	}

	ray(const math::vec<3>& origin, const math::vec<3>& direction) : origin(origin), direction(direction)
	{ 
		assert(math::is_normalized(direction));
	}

	math::vec<3> point_at(float t) const { return origin + direction * t; }

	const math::vec<3> origin;
	const math::vec<3> direction;
};

struct intersection
{
	math::vec<3> position;    // position of the intersection with the surface
	math::vec<3> normal;      // the geometry normal
	float t;                  // distance along the ray
	size_t material_index;	  // the index to the material in the scene
};

struct sphere
{
	math::vec<3> position;
	float radius;
};

bool intersect_ray_sphere(const ray& r, float t_min, float t_max, const sphere& sphere, float* out_t)
{
	const math::vec<3> oc = r.origin - sphere.position;
	const float b = math::dot(oc, r.direction);
	const float c = math::dot(oc, oc) - (sphere.radius * sphere.radius);
	const float discriminant = b * b - c;
	if (discriminant > 0)
	{
		const float discriminant_sqrt = std::sqrt(discriminant);

		float t = (-b - discriminant_sqrt);
		if (t < t_max && t > t_min)
		{
			*out_t = t;

			return true;
		}

		t = (-b + discriminant_sqrt);
		if (t < t_max && t > t_min)
		{
			*out_t = t;

			return true;
		}
	}

	return false;
}

struct camera
{
	camera(float aspect_ratio) : aspect_ratio(aspect_ratio)
	{
		const float fovy = math::pi / 3;
		const float near_plane_distance = 0.1f;
		const float far_plane_distance = 128.0f;

		math::vec<3> at = { 0, 0, 0 };
		eye = { 0, 2, 3 };
		math::vec<3> up = { 0, 1, 0 };

		math::vec<3> forward = math::normalize(eye - at);
		math::vec<3> right = math::normalize(math::cross(up, forward));
		up = math::cross(forward, right);

		view = math::create_look_at_rh(at, eye, up);
		proj = math::create_perspective_fov_rh(fovy, aspect_ratio, near_plane_distance, far_plane_distance);
		view_proj = math::multiply(view, proj);
		inverse_view_proj = math::inverse(view_proj);
	}

	ray create_ray(float u, float v) const
	{
		const math::vec<3> point = math::transform_point(inverse_view_proj, { u * 2 - 1, v * 2 - 1, 0 });
		return { eye, math::normalize(point - eye) };
	}

	math::mat<4> view;
	math::mat<4> proj;
	math::mat<4> view_proj;
	math::mat<4> inverse_view_proj;
	float aspect_ratio;
	math::vec<3> eye;
};

struct material
{
	math::vec<3> base_color = { 1.0f, 1.0f, 1.0f };
	bool is_mirror = false;
};

struct point_light
{
	math::vec<3> position;
	math::vec<3> intensity;
};

struct sphere_area_light
{
	math::vec<3> position;
	float radius;
	math::vec<3> intensity;
};

struct constant_light
{
	math::vec<3> radiance;
};

struct scene
{
	std::vector<point_light> point_lights;
	std::vector<sphere_area_light> sphere_area_lights;
	std::vector<sphere> spheres;
	std::vector<material> sphere_materials; 
	constant_light constant_light;

	bool intersect(const ray& ray, intersection* out_intersection) const
	{
		const float k_min_t = 0.001f; // std::numeric_limits<float>::epsilon();
		const float k_max_t = std::numeric_limits<float>::infinity();

		bool intersection_found = false;

		float t_closest = k_max_t;

		for (size_t i = 0; i < spheres.size(); ++i)
		{
			float t;
			if (intersect_ray_sphere(ray, k_min_t, t_closest, spheres[i], &t))
			{
				intersection_found = true;

				out_intersection->position = ray.point_at(t);
				out_intersection->normal = (out_intersection->position - spheres[i].position) / spheres[i].radius;
				out_intersection->t = t;
				out_intersection->material_index = i;

				t_closest = t;
			}
		}

		return intersection_found;
	}

	bool intersect(const ray& ray) const
	{
		const float k_min_t = 0.001f; // std::numeric_limits<float>::epsilon();
		const float k_max_t = std::numeric_limits<float>::infinity();

		for (size_t i = 0; i < spheres.size(); ++i)
		{
			float t;
			if (intersect_ray_sphere(ray, k_min_t, k_max_t, spheres[i], &t))
			{
				return true;
			}
		}

		return false;
	}
};

struct normal_renderer
{
	static math::vec<3> radiance(const scene& scene, const ray& ray, unsigned* inout_ray_count)
	{
		++(*inout_ray_count);

		intersection its;
		if (scene.intersect(ray, &its))
		{
			return (its.normal + 1) / 2;
		}

		return { 0 };
	}
};

float random_01()
{
	return rand() / (RAND_MAX + 1.0f);
}

math::vec<3> random_point_on_sphere()
{
	const float theta = 2 * math::pi * random_01();
	// incorrect: samples will be clustered at the poles
	// float phi = math::pi * uniform_random_01()
	const float phi = acos(1 - 2 * random_01());

	const float x = sin(phi) * cos(theta);
	const float y = sin(phi) * sin(theta);
	const float z = cos(phi);

	return { x, y, z };
}

math::vec<3> random_point_on_hemisphere()
{
	return { 0, 0, 1 };
}

inline math::vec<3> spherical_to_cartesian(float sin_theta, float cos_theta, float phi) 
{
	return math::vec<3>(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);
}

math::vec<3> random_point_on_visible_sphere(const math::vec<3>& reference_point, const sphere& sphere, float* pdf)
{
	// https://www.akalin.com/sampling-visible-sphere

	const float distance_to_sphere_center = math::distance(sphere.position, reference_point);
	const math::vec<3> direction_to_sphere = (sphere.position - reference_point) / distance_to_sphere_center;

	const float theta_max = asin(sphere.radius / distance_to_sphere_center);

	const float theta = random_01() * theta_max;
	const float phi = random_01() * 2 * math::pi;

	// Theta is the angle between the sample point on the sphere and the center of the sphere when measured from the reference point
	const float sin_theta = sin(theta);
	const float sin_theta2 = sin_theta * sin_theta;
	const float sin_theta_max = sin(theta_max);
	const float sin_theta_max2 = sin_theta_max * sin_theta_max;

	// Alpha is the angle between the sample point and the reference point when measure from the center of the sphere
	const float cos_alpha = sin_theta2 / sin_theta_max + cos(theta) * sqrt(1 - sin_theta2 / sin_theta_max2);
	const float sin_alpha = std::sqrt(std::max(0.0f, 1.0f - cos_alpha * cos_alpha));

	// A point on the sphere as if observing from along the positive Z direction
	math::vec<3> point_on_sphere_os = math::vec<3>(sin_alpha * std::cos(phi), sin_alpha * std::sin(phi), cos_alpha) * sphere.radius;

	math::vec<3> v, u;
	math::orthonormal_basis(direction_to_sphere, &v, &u);

	*pdf = 1 / (2 * math::pi * (1 - std::cos(theta_max)));

	return sphere.position + v * point_on_sphere_os.x + u * point_on_sphere_os.y + direction_to_sphere * point_on_sphere_os.z;
}

struct whitted_renderer
{
	math::vec<3> radiance(const scene& scene, const ray& incident_ray, unsigned* inout_ray_count)
	{
		++(*inout_ray_count);

		math::vec<3> L = { 0 };

		intersection its;
		if (scene.intersect(incident_ray, &its))
		{
			if (scene.sphere_materials[its.material_index].is_mirror)
			{
				if (++_depth < _depth_max)
				{
					const math::vec<3> reflection_direction = math::reflect(incident_ray.direction, its.normal);
					const math::vec<3> f = scene.sphere_materials[its.material_index].base_color;
					L += f * radiance(scene, { its.position, reflection_direction }, inout_ray_count);
				}
			}
			else
			{
				for (const point_light& point_light : scene.point_lights)
				{
					const float distance_to_light = math::distance(point_light.position, its.position);
					const math::vec<3> direction_to_light = (point_light.position - its.position) / distance_to_light;

					++(*inout_ray_count);

					if (!scene.intersect({ its.position, direction_to_light }))
					{
						const math::vec<3> f = scene.sphere_materials[its.material_index].base_color / math::pi; // lambert
						const float n_dot_l = std::max(0.0f, math::dot(its.normal, direction_to_light));
						const float attentuation = 1 / (distance_to_light * distance_to_light);
						L += f * n_dot_l * point_light.intensity * attentuation;
					}
				}

				for (const sphere_area_light& area_light : scene.sphere_area_lights)
				{
					const int light_samples = 32;
					for (int i = 0; i < light_samples; ++i)
					{
						float pdf = 0.0f;
						const math::vec<3> point_on_sphere = random_point_on_visible_sphere(its.position, { area_light.position, area_light.radius }, &pdf);

						const float distance_to_light = math::distance(point_on_sphere, its.position);
						const math::vec<3> direction_to_light = (point_on_sphere - its.position) / distance_to_light;

						++(*inout_ray_count);

						if (!scene.intersect({ its.position, direction_to_light }))
						{
							const math::vec<3> f = scene.sphere_materials[its.material_index].base_color / math::pi; // lambert
							const float n_dot_l = std::max(0.0f, math::dot(its.normal, direction_to_light));
							const float attentuation = 1 / (distance_to_light * distance_to_light);
							L += f * n_dot_l * (area_light.intensity / pdf) * (1.0f / light_samples);
						}
					}
				}

				// constant environment light
				{
					const int light_samples = 32;
					for (int i = 0; i < light_samples; ++i)
					{
						const math::vec<3> direction_to_light = random_point_on_sphere();

						++(*inout_ray_count);

						if (!scene.intersect({ its.position, direction_to_light }))
						{
							const math::vec<3> f = scene.sphere_materials[its.material_index].base_color / math::pi; // lambert
							const float n_dot_l = std::max(0.0f, math::dot(its.normal, direction_to_light));
							// http://corysimon.github.io/articles/uniformdistn-on-sphere/
							const float sphere_pdf = 1 / (4 * math::pi);
							L += f * n_dot_l * (scene.constant_light.radiance / sphere_pdf) * (1.0f / light_samples);
						}
					}
				}
			}
		}
		else
		{
			L += scene.constant_light.radiance;
		}

		return L;
	}

	int _depth_max = 2;
	int _depth = 0;
};

void render(const scene& scene, image* image, unsigned* inout_ray_count)
{
	camera camera(static_cast<float>(image->width) / image->height);

	const unsigned num_threads = std::max(1u, std::thread::hardware_concurrency());
	tf::Taskflow tf(num_threads);

	std::vector<unsigned> statistics;
	statistics.resize(image->height);

	for (int y = 0; y < image->height; ++y)
	{
		unsigned& ray_count = statistics[y];

		tf::Taskflow::Task task = tf.silent_emplace([camera, scene, image, y, &ray_count]()
		{
			for (int x = 0; x < image->width; ++x)
			{
				//static const int bounds[4] = { 518, 519, 202, 203 };

				//if (!(x >= (image->width - bounds[1]) && x < (image->width - bounds[0]) && (image->height - y) >= bounds[2] && (image->height - y) < bounds[3]))
				//{
				//		static int i = 0; 
				//		++i;
				//		continue;
				//}

				const ray ray = camera.create_ray(
					static_cast<float>(x) / image->width,
					static_cast<float>(y) / image->height);

				whitted_renderer renderer;

				math::vec<3> color = renderer.radiance(scene, ray, &ray_count);

				color = linear_to_srgb(color);

				color = math::saturate(color);

				image->data[image->width * y + x] = {
					static_cast<uint8_t>(255.0f * color.z),
					static_cast<uint8_t>(255.0f * color.y),
					static_cast<uint8_t>(255.0f * color.x),
				};
			}
		});
	}

	tf.dispatch();
	tf.wait_for_all();

	for (const unsigned& ray_count : statistics)
	{
		*inout_ray_count += ray_count;
	}
}

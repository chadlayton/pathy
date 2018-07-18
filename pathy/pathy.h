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
	sphere(const math::vec<3>& center, float radius) :
		center(center),
		radius(radius),
		radius2(radius * radius),
		inverse_radius(1.0f / radius) {}

	const math::vec<3> center;
	const float radius;
	const float radius2;
	const float inverse_radius;
};

bool intersect_ray_sphere(const ray& r, float t_min, float t_max, const sphere& sphere, float* out_t)
{
	const math::vec<3> oc = r.origin - sphere.center;
	const float b = math::dot(oc, r.direction);
	const float c = math::dot(oc, oc) - sphere.radius2;
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
	math::vec<3> base_color;
	bool specular;
};

struct point_light
{
	math::vec<3> position;
	math::vec<3> color;	// TODO: Would be nice if this could be specified in terms of energy.
};

struct scene
{
	std::vector<point_light> point_lights;
	std::vector<sphere> spheres;
	std::vector<material> sphere_materials; 

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
				out_intersection->normal = (out_intersection->position - spheres[i].center) * spheres[i].inverse_radius;
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
	static math::vec<3> radiance(const scene& scene, const ray& ray) 
	{
		intersection its;
		if (scene.intersect(ray, &its))
		{
			return (its.normal + 1) / 2;
		}

		return { 0 };
	}
};

struct whitted_renderer
{
	int depth = 0;

	// Sample the incident radiance along a ray
	math::vec<3> radiance(const scene& scene, const ray& ray, unsigned* inout_ray_count)
	{
		++(*inout_ray_count);

		intersection its;
		if (scene.intersect(ray, &its))
		{
			math::vec<3> L = { 0 };

			for (const point_light& point_light : scene.point_lights)
			{
				const float distance_to_light = math::distance(point_light.position, its.position);
				const math::vec<3> direction_to_light = (point_light.position - its.position) / distance_to_light;

				++(*inout_ray_count);
				
				if (!scene.intersect({ its.position, direction_to_light }))
				{
					const float n_dot_l = std::max(0.0f, math::dot(its.normal, direction_to_light));
					const float attentuation = 1 / (distance_to_light * distance_to_light);
					L += scene.sphere_materials[its.material_index].base_color * n_dot_l * point_light.color * attentuation;
				}
			}

			if (depth++ < 4)
			{
				if (scene.sphere_materials[its.material_index].specular) L += radiance(scene, { its.position, math::normalize(math::reflect(ray.direction, its.normal)) }, inout_ray_count);
			}

			return L;
		}

		return { 0 };
	}
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#undef min
#undef max

using namespace Gdiplus;

#pragma comment (lib, "gdiplus.lib")

#include "pathy.h"
#include "benchmark.h"
#include "tinyxml2.h"

image g_image(640, 480);
scene g_scene;

VOID OnPaint(HDC hdc)
{
	{
		unsigned ray_count = 0;

		benchmark::timer timer;
		timer.start();

		render(g_scene, &g_image, &ray_count);

		const double time_seconds = timer.stop() * 0.001;

		printf("completed in %.2f seconds. %u rays cast (%.2f million rays/second).", time_seconds, ray_count, (ray_count * 0.000001) / time_seconds);
	}

	Bitmap bmp(g_image.width, g_image.height, g_image.pitch, PixelFormat24bppRGB, reinterpret_cast<BYTE*>(&g_image.data[0]));
	bmp.RotateFlip(RotateFlipType::Rotate180FlipX);

	Graphics graphics(hdc);
	graphics.DrawImage(&bmp, 0, 0);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		OnPaint(hdc);
		EndPaint(hWnd, &ps);

		return 0;
	}
	case WM_DESTROY:
	{
		PostQuitMessage(0);

		return 0;
	}
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

scene load_scene(const char* filepath)
{
	scene scene;

	tinyxml2::XMLDocument scene_xml;
	tinyxml2::XMLError err = scene_xml.LoadFile(filepath);
	if (err != tinyxml2::XML_SUCCESS)
	{
		std::cerr << "failed to open " << filepath << std::endl;

		return scene;
	}

	tinyxml2::XMLElement* scene_element = scene_xml.FirstChildElement("scene");
	if (!scene_element)
	{
		std::cerr << "the scene " << filepath << " is invalid." << std::endl;

		return scene;
	}

	for (const tinyxml2::XMLElement* scene_child_element = scene_element->FirstChildElement();
		scene_child_element;
		scene_child_element = scene_child_element->NextSiblingElement())
	{
		if (strcmp(scene_child_element->Name(), "emitter") == 0)
		{
			if (strcmp(scene_child_element->Attribute("type"), "point") == 0)
			{
				math::vec<3> position = { 0.0f, 0.0f, 0.0f };
				// The radiant intensity in units of power per unit steradian
				math::vec<3> intensity = { 1.0f, 1.0f, 1.0f };

				for (const tinyxml2::XMLElement* emitter_child_element = scene_child_element->FirstChildElement();
					emitter_child_element;
					emitter_child_element = emitter_child_element->NextSiblingElement())
				{
					if (strcmp(emitter_child_element->Name(), "point") == 0)
					{
						if (strcmp(emitter_child_element->Attribute("name"), "position") == 0)
						{
							position.x = emitter_child_element->FloatAttribute("x");
							position.y = emitter_child_element->FloatAttribute("y");
							position.z = emitter_child_element->FloatAttribute("z");
						}
						else
						{
							assert(false);
						}
					}
					else if (strcmp(emitter_child_element->Name(), "rgb") == 0)
					{
						if (strcmp(emitter_child_element->Attribute("name"), "intensity") == 0)
						{
							const char* value = emitter_child_element->Attribute("value");
							if (sscanf_s(value, "%f, %f, %f", &intensity[0], &intensity[1], &intensity[2]) != 3)
							{
								assert(false);
							}
						}
						else
						{
							assert(false);
						}
					}
					else
					{
						assert(false);
					}
				}

				scene.point_lights.push_back({ position, intensity });
			}
			else if (strcmp(scene_child_element->Attribute("type"), "constant") == 0)
			{
				// The emitted radiance in units of power per unit area per unit steradian
				math::vec<3> radiance = { 0 };

				for (const tinyxml2::XMLElement* emitter_child_element = scene_child_element->FirstChildElement();
					emitter_child_element;
					emitter_child_element = emitter_child_element->NextSiblingElement())
				{
					if (strcmp(emitter_child_element->Name(), "rgb") == 0)
					{
						if (strcmp(emitter_child_element->Attribute("name"), "radiance") == 0)
						{
							const char* value = emitter_child_element->Attribute("value");
							if (sscanf_s(value, "%f, %f, %f", &radiance[0], &radiance[1], &radiance[2]) != 3)
							{
								assert(false);
							}
						}
						else
						{
							assert(false);
						}
					}
					else
					{
						assert(false);
					}
				}

				scene.constant_light.radiance = radiance;
			}
			else
			{
				std::cerr << "emitter has unsupported type: " << scene_child_element->Attribute("type") << std::endl;

				continue;
			}
		}
	}

	for (tinyxml2::XMLElement* shape_element = scene_element->FirstChildElement("shape");
		shape_element;
		shape_element = shape_element->NextSiblingElement("shape"))
	{
		if (strcmp(shape_element->Attribute("type"), "sphere") != 0)
		{
			std::cerr << "shape has unsupported type: " << shape_element->Attribute("type") << std::endl;

			continue;
		}

		math::vec<3> translate = { 0.0f, 0.0f, 0.0f };

		if (tinyxml2::XMLElement* transform_element = shape_element->FirstChildElement("transform"))
		{
			if (tinyxml2::XMLElement* translation_element = transform_element->FirstChildElement("translate"))
			{
				translate.x = translation_element->FloatAttribute("x");
				translate.y = translation_element->FloatAttribute("y");
				translate.z = translation_element->FloatAttribute("z");
			}
		}

		float radius = 1.0f;

		for (tinyxml2::XMLElement* float_element = shape_element->FirstChildElement("float");
			float_element;
			float_element = float_element->NextSiblingElement("float"))
		{
			if (strcmp(float_element->Attribute("name"), "radius") == 0)
			{
				radius = float_element->FloatAttribute("value");
				break;
			}
		}

		if (tinyxml2::XMLElement* emitter_element = shape_element->FirstChildElement("emitter"))
		{
			if (strcmp(emitter_element->Attribute("type"), "area") != 0)
			{
				std::cerr << "emitter has unsupported type: " << emitter_element->Attribute("type") << std::endl;

				continue;
			}

			// The radiant intensity in units of power per unit steradian
			math::vec<3> intensity = { 1.0f, 1.0f, 1.0f };

			for (tinyxml2::XMLElement* rgb_element = emitter_element->FirstChildElement("rgb");
				rgb_element;
				rgb_element = rgb_element->NextSiblingElement("rgb"))
			{
				if (strcmp(rgb_element->Attribute("name"), "intensity") == 0)
				{
					if (sscanf_s(rgb_element->Attribute("value"), "%f, %f, %f", &intensity.x, &intensity.y, &intensity.z) != 3)
					{
						std::cerr << "failed to parse intensity: " << rgb_element->Attribute("value") << std::endl;

						continue;
					}
					break;
				}
			}

			sphere_area_light sphere_area_light{ translate, radius, intensity };
			scene.sphere_area_lights.push_back(sphere_area_light);
		}
		else if (tinyxml2::XMLElement* bsdf_element = shape_element->FirstChildElement("bsdf"))
		{
			material material;

			if (strcmp(bsdf_element->Attribute("type"), "diffuse") == 0)
			{
				math::vec<3> reflectance = { 1.0f, 1.0f, 1.0f };

				for (tinyxml2::XMLElement* rgb_element = bsdf_element->FirstChildElement("rgb");
					rgb_element;
					rgb_element = rgb_element->NextSiblingElement("rgb"))
				{
					if (strcmp(rgb_element->Attribute("name"), "reflectance") == 0)
					{
						if (sscanf_s(rgb_element->Attribute("value"), "%f, %f, %f", &reflectance.x, &reflectance.y, &reflectance.z) != 3)
						{
							std::cerr << "failed to parse reflectance: " << rgb_element->Attribute("value") << std::endl;

							continue;
						}
						break;
					}
				}

				material.base_color = reflectance;
			}
			else if (strcmp(bsdf_element->Attribute("type"), "conductor") == 0)
			{
				math::vec<3> specular_reflectance = { 1.0f, 1.0f, 1.0f };

				for (tinyxml2::XMLElement* rgb_element = bsdf_element->FirstChildElement("rgb");
					rgb_element;
					rgb_element = rgb_element->NextSiblingElement("rgb"))
				{
					if (strcmp(rgb_element->Attribute("name"), "specularReflectance") == 0)
					{
						if (sscanf_s(rgb_element->Attribute("value"), "%f, %f, %f", &specular_reflectance.x, &specular_reflectance.y, &specular_reflectance.z) != 3)
						{
							std::cerr << "failed to parse specularReflectance: " << rgb_element->Attribute("value") << std::endl;

							continue;
						}
						break;
					}
				}

				material.base_color = specular_reflectance;
				material.is_mirror = true;
			}
			else
			{
				std::cerr << "bsdf has unsupported type: " << bsdf_element->Attribute("type") << std::endl;

				continue;
			}

			sphere sphere{ translate, radius };
			scene.spheres.push_back(sphere);
			scene.sphere_materials.push_back(material);
		}
	}

	return scene;
}

int main()
{
	g_scene = load_scene("aras.xml");

	GdiplusStartupInput gdiplus_startup_input;
	ULONG_PTR gdiplus_token;
	GdiplusStartup(&gdiplus_token, &gdiplus_startup_input, NULL);

	HINSTANCE hInstance = GetModuleHandle(NULL);
	WNDCLASS wndclass = {};
	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = hInstance;
	wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = TEXT("pathy");

	RegisterClass(&wndclass);

	DWORD window_style = WS_OVERLAPPEDWINDOW;

	RECT window_rect = { 0, 0, g_image.width, g_image.height };
	AdjustWindowRect(&window_rect, window_style, FALSE);

	HWND hWnd = CreateWindow(
		TEXT("pathy"),
		TEXT("pathy"),
		window_style,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		window_rect.right - window_rect.left,
		window_rect.bottom - window_rect.top,
		NULL,
		NULL,
		hInstance,
		NULL);

	ShowWindow(hWnd, SW_SHOWNORMAL);
	UpdateWindow(hWnd);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	GdiplusShutdown(gdiplus_token);

	return static_cast<int>(msg.wParam);
}
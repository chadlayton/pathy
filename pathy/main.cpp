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

		printf("completed in %.2f seconds (%.2f million rays/second)", time_seconds, (ray_count * 0.000001) / time_seconds);
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
				math::vec<3> position = { 0 };

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
					}
				}

				scene.point_lights.push_back({ position, { 0.9f, 0.9f, 0.9f } });
			}
			else if (strcmp(scene_child_element->Attribute("type"), "constant") == 0)
			{
				// unsupported
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

		math::vec<3> translate;
		if (tinyxml2::XMLElement* transform_element = shape_element->FirstChildElement("transform"))
		{
			if (tinyxml2::XMLElement* translation_element = transform_element->FirstChildElement("translate"))
			{
				translate.x = translation_element->FloatAttribute("x");
				translate.y = translation_element->FloatAttribute("y");
				translate.z = translation_element->FloatAttribute("z");
			}
			else
			{
				std::cerr << "transform is missing translate element" << std::endl;

				continue;
			}
		}
		else
		{
			std::cerr << "shape is missing a transform element." << std::endl;

			continue;
		}

		float radius;
		tinyxml2::XMLElement* float_element = nullptr;
		for (float_element = shape_element->FirstChildElement("float");
			float_element;
			float_element = float_element->NextSiblingElement("float"))
		{
			if (strcmp(float_element->Attribute("name"), "radius") == 0)
			{
				radius = float_element->FloatAttribute("value");
				break;
			}
		}
		if (!float_element)
		{
			std::cerr << "shape is missing a radius element" << std::endl;

			continue;
		}

		material material;

		if (tinyxml2::XMLElement* bsdf_element = shape_element->FirstChildElement("bsdf"))
		{
			if (strcmp(bsdf_element->Attribute("type"), "diffuse") == 0)
			{
				math::vec<3> reflectance;
				tinyxml2::XMLElement* rgb_element = nullptr;
				for (rgb_element = bsdf_element->FirstChildElement("rgb");
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
				if (!rgb_element)
				{
					std::cerr << "diffuse is missing a rgb reflectance element" << std::endl;

					continue;
				}

				material.base_color = reflectance;
				material.specular = false;
			}
			else if (strcmp(bsdf_element->Attribute("type"), "roughconductor") == 0)
			{
				math::vec<3> specular_reflectance;
				tinyxml2::XMLElement* rgb_element = nullptr;
				for (rgb_element = bsdf_element->FirstChildElement("rgb");
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
				if (!rgb_element)
				{
					std::cerr << "roughconductor is missing a rgb specularReflectance element" << std::endl;

					continue;
				}

				float alpha;
				tinyxml2::XMLElement* float_element = nullptr;
				for (float_element = bsdf_element->FirstChildElement("float");
					float_element;
					float_element = float_element->NextSiblingElement("float"))
				{
					if (strcmp(float_element->Attribute("name"), "alpha") == 0)
					{
						alpha = float_element->FloatAttribute("value");

						break;
					}
				}
				if (!float_element)
				{
					std::cerr << "roughconductor is missing a float alpha element" << std::endl;

					continue;
				}

				material.base_color = specular_reflectance;
				material.specular = true;
			}
			//else if (strcmp(bsdf_element->Attribute("type"), "dielectric") == 0)
			//{
			//	float ior;
			//	tinyxml2::XMLElement* float_element = nullptr;
			//	for (float_element = bsdf_element->FirstChildElement("float");
			//		float_element;
			//		float_element = float_element->NextSiblingElement("float"))
			//	{
			//		if (strcmp(float_element->Attribute("name"), "intIOR") == 0)
			//		{
			//			ior = float_element->FloatAttribute("value");

			//			break;
			//		}
			//	}
			//	if (!float_element)
			//	{
			//		std::cerr << "dielectric is missing a float intIOR element" << std::endl;

			//		continue;
			//	}

			//	material.base_color = 0;
			//}
			else
			{
				std::cerr << "bsdf has unsupported type: " << bsdf_element->Attribute("type") << std::endl;

				continue;
			}
		}
		else
		{
			std::cerr << "shape is missing a bsdf element." << std::endl;

			continue;
		}

		sphere sphere(translate, radius);
		scene.spheres.push_back(sphere);
		scene.sphere_materials.push_back(material);
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

	return msg.wParam;
}
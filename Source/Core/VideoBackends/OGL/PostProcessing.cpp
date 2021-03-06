// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"

#include "VideoBackends/OGL/FramebufferManager.h"
#include "VideoBackends/OGL/GLUtil.h"
#include "VideoBackends/OGL/PostProcessing.h"
#include "VideoBackends/OGL/ProgramShaderCache.h"

#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"

namespace OGL
{

static char s_vertex_shader[] =
	"out vec2 uv0;\n"
	"void main(void) {\n"
	"	vec2 rawpos = vec2(gl_VertexID&1, gl_VertexID&2);\n"
	"	gl_Position = vec4(rawpos*2.0-1.0, 0.0, 1.0);\n"
	"	uv0 = rawpos;\n"
	"}\n";

OpenGLPostProcessing::OpenGLPostProcessing()
{
	m_enable = false;
	m_width = 0;
	m_height = 0;

	glGenFramebuffers(1, &m_fbo);
	glGenTextures(1, &m_texture);
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0); // disable mipmaps
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
	FramebufferManager::SetFramebuffer(0);

	CreateHeader();
}

OpenGLPostProcessing::~OpenGLPostProcessing()
{
	m_shader.Destroy();

	glDeleteFramebuffers(1, &m_fbo);
	glDeleteTextures(1, &m_texture);
}

void OpenGLPostProcessing::BindTargetFramebuffer()
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_enable ? m_fbo : 0);
}

void OpenGLPostProcessing::BlitToScreen()
{
	if (!m_enable) return;

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport(0, 0, m_width, m_height);

	m_shader.Bind();

	glUniform4f(m_uniform_resolution, (float)m_width, (float)m_height, 1.0f/(float)m_width, 1.0f/(float)m_height);
	glUniform1ui(m_uniform_time, (GLuint)m_timer.GetTimeElapsed());

	if (m_config.IsDirty())
	{
		for (auto& it : m_config.GetOptions())
		{
			if (it.second.m_dirty)
			{
				switch (it.second.m_type)
				{
				case PostProcessingShaderConfiguration::ConfigurationOption::OptionType::OPTION_BOOL:
					glUniform1i(m_uniform_bindings[it.first], it.second.m_bool_value);
				break;
				case PostProcessingShaderConfiguration::ConfigurationOption::OptionType::OPTION_INTEGER:
					switch (it.second.m_integer_values.size())
					{
					case 1:
						glUniform1i(m_uniform_bindings[it.first], it.second.m_integer_values[0]);
					break;
					case 2:
						glUniform2i(m_uniform_bindings[it.first],
								it.second.m_integer_values[0],
						            it.second.m_integer_values[1]);
					break;
					case 3:
						glUniform3i(m_uniform_bindings[it.first],
								it.second.m_integer_values[0],
								it.second.m_integer_values[1],
						            it.second.m_integer_values[2]);
					break;
					case 4:
						glUniform4i(m_uniform_bindings[it.first],
								it.second.m_integer_values[0],
								it.second.m_integer_values[1],
								it.second.m_integer_values[2],
						            it.second.m_integer_values[3]);
					break;
					}
				break;
				case PostProcessingShaderConfiguration::ConfigurationOption::OptionType::OPTION_FLOAT:
					switch (it.second.m_float_values.size())
					{
					case 1:
						glUniform1f(m_uniform_bindings[it.first], it.second.m_float_values[0]);
					break;
					case 2:
						glUniform2f(m_uniform_bindings[it.first],
								it.second.m_float_values[0],
						            it.second.m_float_values[1]);
					break;
					case 3:
						glUniform3f(m_uniform_bindings[it.first],
								it.second.m_float_values[0],
								it.second.m_float_values[1],
						            it.second.m_float_values[2]);
					break;
					case 4:
						glUniform4f(m_uniform_bindings[it.first],
								it.second.m_float_values[0],
								it.second.m_float_values[1],
								it.second.m_float_values[2],
						            it.second.m_float_values[3]);
					break;
					}
				break;
				}
				it.second.m_dirty = false;
			}
		}
		m_config.SetDirty(false);
	}

	glActiveTexture(GL_TEXTURE0+9);
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void OpenGLPostProcessing::Update(u32 width, u32 height)
{
	ApplyShader();

	if (m_enable && (width != m_width || height != m_height))
	{
		m_width = width;
		m_height = height;

		// alloc texture for framebuffer
		glActiveTexture(GL_TEXTURE0+9);
		glBindTexture(GL_TEXTURE_2D, m_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	}
}

void OpenGLPostProcessing::ApplyShader()
{
	// shader didn't changed
	if (m_config.GetShader() == g_ActiveConfig.sPostProcessingShader)
		return;

	m_enable = false;
	m_shader.Destroy();
	m_uniform_bindings.clear();

	// shader disabled
	if (g_ActiveConfig.sPostProcessingShader == "")
		return;

	// so need to compile shader
	std::string code = m_config.LoadShader();

	if (code == "")
		return;

	code = LoadShaderOptions(code);

	// and compile it
	if (!ProgramShaderCache::CompileShader(m_shader, s_vertex_shader, code.c_str()))
	{
		ERROR_LOG(VIDEO, "Failed to compile post-processing shader %s", m_config.GetShader().c_str());
		return;
	}

	// read uniform locations
	m_uniform_resolution = glGetUniformLocation(m_shader.glprogid, "resolution");
	m_uniform_time = glGetUniformLocation(m_shader.glprogid, "time");

	for (const auto& it : m_config.GetOptions())
	{
		std::string glsl_name = "option_" + it.first;
		m_uniform_bindings[it.first] = glGetUniformLocation(m_shader.glprogid, glsl_name.c_str());
	}

	// successful
	m_enable = true;
}

void OpenGLPostProcessing::CreateHeader()
{
	m_glsl_header =
		// Required variables
		// Shouldn't be accessed directly by the PP shader
		// Texture sampler
		"SAMPLER_BINDING(8) uniform sampler2D samp8;\n"
		"SAMPLER_BINDING(9) uniform sampler2D samp9;\n"

		// Output variable
		"out float4 ocol0;\n"
		// Input coordinates
		"in float2 uv0;\n"
		// Resolution
		"uniform float4 resolution;\n"
		// Time
		"uniform uint time;\n"

		// Interfacing functions
		"float4 Sample()\n"
		"{\n"
			"\treturn texture(samp9, uv0);\n"
		"}\n"

		"float4 SampleLocation(float2 location)\n"
		"{\n"
			"\treturn texture(samp9, location);\n"
		"}\n"

		"#define SampleOffset(offset) textureOffset(samp9, uv0, offset)\n"

		"float4 SampleFontLocation(float2 location)\n"
		"{\n"
			"\treturn texture(samp8, location);\n"
		"}\n"

		"float2 GetResolution()\n"
		"{\n"
			"\treturn resolution.xy;\n"
		"}\n"

		"float2 GetInvResolution()\n"
		"{\n"
			"\treturn resolution.zw;\n"
		"}\n"

		"float2 GetCoordinates()\n"
		"{\n"
			"\treturn uv0;\n"
		"}\n"

		"uint GetTime()\n"
		"{\n"
			"\treturn time;\n"
		"}\n"

		"void SetOutput(float4 color)\n"
		"{\n"
			"\tocol0 = color;\n"
		"}\n"

		"#define GetOption(x) (option_##x)\n"
		"#define OptionEnabled(x) (option_##x != 0)\n";
}

std::string OpenGLPostProcessing::LoadShaderOptions(const std::string& code)
{
	std::string glsl_options = "";
	m_uniform_bindings.clear();

	for (const auto& it : m_config.GetOptions())
	{
		if (it.second.m_type == PostProcessingShaderConfiguration::ConfigurationOption::OptionType::OPTION_BOOL)
		{
			glsl_options += StringFromFormat("uniform int     option_%s;\n", it.first.c_str());
		}
		else if (it.second.m_type == PostProcessingShaderConfiguration::ConfigurationOption::OptionType::OPTION_INTEGER)
		{
			u32 count = static_cast<u32>(it.second.m_integer_values.size());
			if (count == 1)
				glsl_options += StringFromFormat("uniform int     option_%s;\n", it.first.c_str());
			else
				glsl_options += StringFromFormat("uniform int%d   option_%s;\n", count, it.first.c_str());
		}
		else if (it.second.m_type == PostProcessingShaderConfiguration::ConfigurationOption::OptionType::OPTION_FLOAT)
		{
			u32 count = static_cast<u32>(it.second.m_float_values.size());
			if (count == 1)
				glsl_options += StringFromFormat("uniform float   option_%s;\n", it.first.c_str());
			else
				glsl_options += StringFromFormat("uniform float%d option_%s;\n", count, it.first.c_str());
		}

		m_uniform_bindings[it.first] = 0;
	}

	return m_glsl_header + glsl_options + code;
}

}  // namespace OGL

#include "Rendering/MeshFactory.h"

//#include <assimp/Importer.hpp>
//#include <assimp/scene.h>
//#include <assimp/postprocess.h>

MeshData MeshFactory::CreateFromFile(const std::string& Filename, bool MergeMeshes, bool LeftHanded) noexcept
{
	/*using namespace std;

	// Set import flags
	Uint32 flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace;
	if (leftHanded)
	{
		flags |= aiProcess_ConvertToLeftHanded;
	}
	flags = flags & ~aiProcess_FlipWindingOrder;

	// Load scene
	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(filename, flags);

	// Extract scene-data
	MeshData data;
	if (pScene)
	{
		if (pScene->HasMeshes())
		{
			LOG_DEBUG_INFO("[LAMBDA ENGINE] Loading Scene with '%u' meshes\n", pScene->mNumMeshes);

			size_t vertexOffset = 0;
			size_t indexOffset = 0;
			uint32 numMeshesToLoad = (mergeMeshes) ? pScene->mNumMeshes : 1;
			for (uint32 m = 0; m < numMeshesToLoad; m++)
			{
				const aiMesh* pMesh = pScene->mMeshes[m];
				if (!pMesh->HasNormals())
				{
					LOG_DEBUG_WARNING("[LAMBDA ENGINE] Mesh does not have normals\n");
				}
				if (!pMesh->HasTextureCoords(0))
				{
					LOG_DEBUG_WARNING("[LAMBDA ENGINE] Mesh does not have texcoords\n");
				}

				if (pMesh)
				{
					if (pMesh->HasFaces())
					{
						// Get number of vertices and resize buffer
						size_t vertCount = pMesh->mNumVertices;
						// Vertexoffset is used when there are more than one mesh in the scene and we want to merge all the meshes
						data.Vertices.resize(vertexOffset + vertCount);
						for (size_t i = 0; i < vertCount; i++)
						{
							data.Vertices[vertexOffset + i].Position = vec3(pMesh->mVertices[i].x, pMesh->mVertices[i].y, pMesh->mVertices[i].z);
							if (pMesh->HasNormals())
							{
								data.Vertices[vertexOffset + i].Normal = vec3(pMesh->mNormals[i].x, pMesh->mNormals[i].y, pMesh->mNormals[i].z);
							}
							if (pMesh->HasTangentsAndBitangents())
							{
								data.Vertices[vertexOffset + i].Tangent = vec3(pMesh->mTangents[i].x, pMesh->mTangents[i].y, pMesh->mTangents[i].z);
							}
							if (pMesh->HasTextureCoords(0))
							{
								data.Vertices[vertexOffset + i].TexCoord = vec2(pMesh->mTextureCoords[0][i].x, pMesh->mTextureCoords[0][i].y);
							}
						}

						// Get number of indices and resize indexbuffer
						size_t triCount = pMesh->mNumFaces;
							
						// Indexoffset is used when there are more than one mesh in the scene and we want to merge all the meshes
						data.Indices.resize(indexOffset + (triCount * 3));
						for (size_t i = 0; i < triCount; i++)
						{
							data.Indices[indexOffset + (i * 3) + 0] = uint32(vertexOffset + pMesh->mFaces[i].mIndices[0]);
							data.Indices[indexOffset + (i * 3) + 1] = uint32(vertexOffset + pMesh->mFaces[i].mIndices[1]);
							data.Indices[indexOffset + (i * 3) + 2] = uint32(vertexOffset + pMesh->mFaces[i].mIndices[2]);
						}

						// Increase offsets
						if (mergeMeshes)
						{
							vertexOffset += vertCount;
							indexOffset += triCount * 3;
						}
					}
				}
			}
		}
		else
		{
			LOG_DEBUG_ERROR("[LAMBDA ENGINE] File '%s' does not have any meshes\n", filename.c_str());
			return data;
		}
	}
	else
	{
		const char* pErrorMessage = importer.GetErrorString();
		LOG_SYSTEM_PRINT("[LAMBDA ENGINE] Failed to load file '%s'. Message: %s\n", filename.c_str(), pErrorMessage);
		return data;
	}

	LOG_SYSTEM_PRINT("[LAMBDA ENGINE] Loaded mesh with %d vertices and %d indices. Triangles: %d\n", data.Vertices.size(), data.Indices.size(), data.Indices.size() / 3);
	return data;*/
	return MeshData();
}

MeshData MeshFactory::CreateCube(Float32 Width, Float32 Height, Float32 Depth) noexcept
{
	Float32 HalfWitdth	= Width * 0.5f;
	Float32 HalfHeight	= Height * 0.5f;
	Float32 HalfDepth	= Depth * 0.5f;

	MeshData Cube;
	Cube.Vertices =
	{
		// FRONT FACE
		{ XMFLOAT3(-HalfWitdth,  HalfHeight, -HalfDepth), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT3(1.0f,  0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3( HalfWitdth,  HalfHeight, -HalfDepth), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT3(1.0f,  0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(-HalfWitdth, -HalfHeight, -HalfDepth), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT3(1.0f,  0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3( HalfWitdth, -HalfHeight, -HalfDepth), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT3(1.0f,  0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },

		// BACK FACE
		{ XMFLOAT3( HalfWitdth,  HalfHeight,  HalfDepth), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(-HalfWitdth,  HalfHeight,  HalfDepth), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3( HalfWitdth, -HalfHeight,  HalfDepth), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-HalfWitdth, -HalfHeight,  HalfDepth), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },

		// RIGHT FACE
		{ XMFLOAT3(HalfWitdth,  HalfHeight, -HalfDepth), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f,  0.0f, 1.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(HalfWitdth,  HalfHeight,  HalfDepth), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f,  0.0f, 1.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(HalfWitdth, -HalfHeight, -HalfDepth), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f,  0.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(HalfWitdth, -HalfHeight,  HalfDepth), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f,  0.0f, 1.0f), XMFLOAT2(1.0f, 1.0f) },

		// LEFT FACE
		{ XMFLOAT3(-HalfWitdth,  HalfHeight, -HalfDepth), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(-HalfWitdth,  HalfHeight,  HalfDepth), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(-HalfWitdth, -HalfHeight, -HalfDepth), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-HalfWitdth, -HalfHeight,  HalfDepth), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },

		// TOP FACE
		{ XMFLOAT3(-HalfWitdth,  HalfHeight,  HalfDepth), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT3(1.0f,  0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3( HalfWitdth,  HalfHeight,  HalfDepth), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT3(1.0f,  0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(-HalfWitdth,  HalfHeight, -HalfDepth), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT3(1.0f,  0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3( HalfWitdth,  HalfHeight, -HalfDepth), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT3(1.0f,  0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },

		// BOTTOM FACE
		{ XMFLOAT3(-HalfWitdth, -HalfHeight, -HalfDepth), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT3(-1.0f,  0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3( HalfWitdth, -HalfHeight, -HalfDepth), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT3(-1.0f,  0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(-HalfWitdth, -HalfHeight,  HalfDepth), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT3(-1.0f,  0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3( HalfWitdth, -HalfHeight,  HalfDepth), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT3(-1.0f,  0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	};

	Cube.Indices =
	{
		// FRONT FACE
		0, 1, 2,
		1, 3, 2,

		// BACK FACE
		5, 5, 6,
		5, 7, 6,

		// RIGHT FACE
		8, 9, 10,
		9, 11, 10,

		// LEFT FACE
		14, 13, 12,
		14, 15, 13,

		// TOP FACE
		16, 17, 18,
		17, 19, 18,

		// BOTTOM FACE
		20, 21, 22,
		21, 23, 22
	};

	return Cube;
}

MeshData MeshFactory::CreatePlane(Uint32 width, Uint32 height) noexcept
{
	/*using namespace std;

	MeshData data;
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	data.Vertices.resize((width + 1) * (height + 1));
	data.Indices.resize((width * height) * 6);

	//Size of each quad, size of the plane will always be between -0.5 and 0.5
	vec2 quadSize = vec2(1.0f / float(width), 1.0f / float(height));
	vec2 uvQuadSize = vec2(1.0f / float(width), 1.0f / float(height));

	//Initialize Vertices
	for (uint32 x = 0; x <= width; x++)
	{
		for (uint32 y = 0; y <= height; y++)
		{
			int32 v = ((1 + height) * x) + y;
			data.Vertices[v].Position = vec3(0.5f - (quadSize.x * x), 0.5f - (quadSize.y * y), 0.0f);
			data.Vertices[v].Normal = vec3(0.0f, 0.0f, 1.0f);
			data.Vertices[v].Tangent = vec3(1.0f, 0.0f, 0.0f);
			data.Vertices[v].TexCoord = vec2(0.0f + (uvQuadSize.x * x), 0.0f + (uvQuadSize.y * y));
		}
	}

	//Initialize Indices
	for (uint8 x = 0; x < width; x++)
	{
		for (uint8 y = 0; y < height; y++)
		{
			int32 quad = (height * x) + y;
			data.Indices[(quad * 6) + 0] = (x * (1 + height)) + y + 1;
			data.Indices[(quad * 6) + 1] = (data.Indices[quad * 6] + 2 + (height - 1));
			data.Indices[(quad * 6) + 2] = data.Indices[quad * 6] - 1;
			data.Indices[(quad * 6) + 3] = data.Indices[(quad * 6) + 1];
			data.Indices[(quad * 6) + 4] = data.Indices[(quad * 6) + 1] - 1;
			data.Indices[(quad * 6) + 5] = data.Indices[(quad * 6) + 2];
		}
	}

	//Resize array
	data.Vertices.shrink_to_fit();
	data.Indices.shrink_to_fit();

	return data;*/
	return MeshData();
}

MeshData MeshFactory::CreateSphere(Uint32 Subdivisions, Float32 Radius) noexcept
{
	MeshData Sphere;
	Sphere.Vertices.resize(12);

	// VERTICES
	Float32 T = (1.0f + sqrt(5.0f)) / 2.0f;
	Sphere.Vertices[0].Position		= XMFLOAT3(-1.0f,  T,     0.0f);
	Sphere.Vertices[1].Position		= XMFLOAT3( 1.0f,  T,     0.0f);
	Sphere.Vertices[2].Position		= XMFLOAT3(-1.0f, -T,     0.0f);
	Sphere.Vertices[3].Position		= XMFLOAT3( 1.0f, -T,     0.0f);
	Sphere.Vertices[4].Position		= XMFLOAT3( 0.0f, -1.0f,  T);
	Sphere.Vertices[5].Position		= XMFLOAT3( 0.0f,  1.0f,  T);
	Sphere.Vertices[6].Position		= XMFLOAT3( 0.0f, -1.0f, -T);
	Sphere.Vertices[7].Position		= XMFLOAT3( 0.0f,  1.0f, -T);
	Sphere.Vertices[8].Position		= XMFLOAT3( T,     0.0f, -1.0f);
	Sphere.Vertices[9].Position		= XMFLOAT3( T,     0.0f,  1.0f);
	Sphere.Vertices[10].Position	= XMFLOAT3(-T,     0.0f, -1.0f);
	Sphere.Vertices[11].Position	= XMFLOAT3(-T,     0.0f,  1.0f);

	// INDICIES
	Sphere.Indices =
	{
		0, 11, 5,
		0, 5,  1,
		0, 1,  7,
		0, 7,  10,
		0, 10, 11,

		1,  5,  9,
		5,  11, 4,
		11, 10, 2,
		10, 7,  6,
		7,  1,  8,

		3, 9, 4,
		3, 4, 2,
		3, 2, 6,
		3, 6, 8,
		3, 8, 9,

		4, 9, 5,
		2, 4, 11,
		6, 2, 10,
		8, 6, 7,
		9, 8, 1,
	};

	if (Subdivisions > 0)
	{
		Subdivide(Sphere, Subdivisions);
	}

	for (Uint32 i = 0; i < static_cast<Uint32>(Sphere.Vertices.size()); i++)
	{
		// Calculate the new position
		XMVECTOR Position = XMLoadFloat3(&Sphere.Vertices[i].Position);
		Position = XMVector3Normalize(Position);

		XMStoreFloat3(&Sphere.Vertices[i].Normal, Position);

		Position = XMVectorScale(Position, Radius);
		XMStoreFloat3(&Sphere.Vertices[i].Position, Position);

		// Calculate uvs
		XMFLOAT2 TexCoords;
		TexCoords.y = (asin(Sphere.Vertices[i].Position.y) / XM_PI) + 0.5f;
		TexCoords.x = (atan2f(Sphere.Vertices[i].Position.z, Sphere.Vertices[i].Position.x) + XM_PI) / (2.0f * XM_PI);
		Sphere.Vertices[i].TexCoord = TexCoords;
	}

	CalculateTangents(Sphere);

	Sphere.Indices.shrink_to_fit();
	Sphere.Vertices.shrink_to_fit();

	return Sphere;
}

MeshData MeshFactory::CreateCone(Uint32 sides, Float32 radius, Float32 height) noexcept
{
	/*
	MeshData data;
	// Num verts = (Sides*2)	(Bottom, since we need unique normals)
	//		    +  Sides	(1 MiddlePoint per side)
	//			+  1		(One middlepoint on the underside)
	size_t vertSize = size_t(sides) * 3 + 1;
	data.Vertices.resize(vertSize);

	// Num indices = (Sides*3*2) (Cap has 'sides' number of tris + sides tris for the sides, each tri has 3 verts)
	size_t indexSize = size_t(sides) * 6;
	data.Indices.resize(indexSize);

	// Angle between verts
	Float32 angle = (pi<float>() * 2.0f) / float(sides);
	Float32 uOffset = 1.0f / float(sides - 1);

	// CREATE VERTICES
	data.Vertices[0].Position = vec3(0.0f, 0.0f, 0.0f);
	data.Vertices[0].Normal = vec3(0.0f, -1.0f, 0.0f);
	data.Vertices[0].TexCoord = vec2(0.25f, 0.25f);

	size_t offset = size_t(sides) + 1;
	size_t topOffset = offset + size_t(sides);
	for (size_t i = 0; i < sides; i++)
	{
		// BOTTOM CAP VERTICES
		Float32 x = cosf((pi<Float32>() / 2.0f) + (angle * i));
		Float32 z = sinf((pi<Float32>() / 2.0f) + (angle * i));

		vec3 pos = normalize(vec3(x, 0.0f, z));
		data.Vertices[i + 1].Position = (pos * radius);
		data.Vertices[i + 1].Normal = vec3(0.0f, -1.0f, 0.0f);
		data.Vertices[i + 1].TexCoord = (vec2(x + 1.0f, z + 1.0f) * 0.25f);

		// BOTTOM SIDE VERTICES
		vec3 normal = normalize(pos + vec3(0.0f, sin(atan(height / radius)), 0.0f));
		data.Vertices[offset + i].Position = data.Vertices[i + 1].Position;
		data.Vertices[offset + i].Normal = normal;
		data.Vertices[offset + i].TexCoord = vec2(0.0f + (uOffset * i), 1.0f);

		// TOP
		data.Vertices[topOffset + i].Position = vec3(0.0f, height, 0.0f);
		data.Vertices[topOffset + i].Normal = normal;
		data.Vertices[topOffset + i].TexCoord = vec2(0.0f + (uOffset * i), 0.25f);
	}

	// BOTTOM CAP INDICES
	size_t index = 0;
	for (uint32 i = 0; i < sides; i++)
	{
		data.Indices[index + 0] = ((i + 1) % sides) + 10;
		data.Indices[index + 1] = i + 1;
		data.Indices[index + 2] = 0;
		index += 3;
	}

	// SIDES INDICES
	for (uint32 i = 0; i < sides; i++)
	{
		data.Indices[index + 0] = uint32(offset) + i;
		data.Indices[index + 1] = uint32(offset) + ((i + 1) % sides);
		data.Indices[index + 2] = uint32(topOffset) + i;
		index += 3;
	}

	//Get tangents
	CalculateTangents(data);

	return data;
	*/
	return MeshData();
}

MeshData MeshFactory::CreatePyramid() noexcept
{
	/*
	MeshData data;
	data.Vertices.resize(16);
	data.Indices.resize(18);

	// FLOOR FACE (Seen from FRONT FACE)
	data.Vertices[0].TexCoord = vec2(0.33f, 0.33f);
	data.Vertices[0].Position = vec3(-0.5f, -0.5f, -0.5f);
	data.Vertices[1].TexCoord = vec2(0.66f, 0.33f);
	data.Vertices[1].Position = vec3(0.5f, -0.5f, -0.5f);
	data.Vertices[2].TexCoord = vec2(0.33f, 0.66f);
	data.Vertices[2].Position = vec3(-0.5f, -0.5f, 0.5f);
	data.Vertices[3].TexCoord = vec2(0.66f, 0.66f);
	data.Vertices[3].Position = vec3(0.5f, -0.5f, 0.5f);

	// TOP VERTICES
	data.Vertices[4].Position =
		data.Vertices[5].Position =
		data.Vertices[6].Position =
		data.Vertices[7].Position = vec3(0.0f, 0.5f, 0.0f);
	data.Vertices[4].TexCoord = vec2(0.495f, 0.0f);
	data.Vertices[5].TexCoord = vec2(0.0f, 0.495f);
	data.Vertices[6].TexCoord = vec2(0.495f, 0.99f);
	data.Vertices[7].TexCoord = vec2(0.99f, 0.495f);

	// BACK
	data.Vertices[8].TexCoord = vec2(0.33f, 0.33f);
	data.Vertices[8].Position = vec3(-0.5f, -0.5f, -0.5f);
	data.Vertices[9].TexCoord = vec2(0.66f, 0.33f);
	data.Vertices[9].Position = vec3(0.5f, -0.5f, -0.5f);

	// FRONT
	data.Vertices[10].TexCoord = vec2(0.33f, 0.66f);
	data.Vertices[10].Position = vec3(-0.5f, -0.5f, 0.5f);
	data.Vertices[11].TexCoord = vec2(0.66f, 0.66f);
	data.Vertices[11].Position = vec3(0.5f, -0.5f, 0.5f);

	// LEFT
	data.Vertices[12].TexCoord = vec2(0.33f, 0.33f);
	data.Vertices[12].Position = vec3(-0.5f, -0.5f, -0.5f);
	data.Vertices[13].TexCoord = vec2(0.33f, 0.66f);
	data.Vertices[13].Position = vec3(-0.5f, -0.5f, 0.5f);

	// RIGHT
	data.Vertices[14].TexCoord = vec2(0.66f, 0.33f);
	data.Vertices[14].Position = vec3(0.5f, -0.5f, -0.5f);
	data.Vertices[15].TexCoord = vec2(0.66f, 0.66f);
	data.Vertices[15].Position = vec3(0.5f, -0.5f, 0.5f);

	// FLOOR FACE
	data.Indices[0] = 2;
	data.Indices[1] = 1;
	data.Indices[2] = 0;
	data.Indices[3] = 2;
	data.Indices[4] = 3;
	data.Indices[5] = 1;

	// BACK FACE
	data.Indices[6] = 8;
	data.Indices[7] = 9;
	data.Indices[8] = 4;

	// LEFT FACE
	data.Indices[9] = 13;
	data.Indices[10] = 12;
	data.Indices[11] = 5;

	// FRONT FACE
	data.Indices[12] = 11;
	data.Indices[13] = 10;
	data.Indices[14] = 6;

	// RIGHT FACE
	data.Indices[15] = 14;
	data.Indices[16] = 15;
	data.Indices[17] = 7;

	data.Indices.shrink_to_fit();
	data.Vertices.shrink_to_fit();

	CalculateHardNormals(data);
	CalculateTangents(data);

	return data;
	*/
	return MeshData();
}

MeshData MeshFactory::CreateCylinder(Uint32 sides, Float32 radius, Float32 height) noexcept
{
	/*
	MeshData data;
	if (sides < 5)
		sides = 5;
	if (height < 0.1f)
		height = 0.1f;
	if (radius < 0.1f)
		radius = 0.1f;

	// Num verts = (Sides*2)	(Top, since we need unique normals) 
	//          + (Sides*2)	(Bottom)
	//		    + 2			(MiddlePoints)
	size_t vertSize = size_t(sides) * 4 + 2;
	data.Vertices.resize(vertSize);

	// Num indices = (Sides*3*2) (Each cap has 'sides' number of tris, each tri has 3 verts)
	//			  + (Sides*6)	(Each side has 6 verts)
	size_t indexSize = size_t(sides) * 12;
	data.Indices.resize(indexSize);

	// Angle between verts
	Float32 angle = (pi<float>() * 2.0f) / float(sides);
	Float32 uOffset = 1.0f / float(sides - 1);
	Float32 halfHeight = height * 0.5f;

	// CREATE VERTICES
	data.Vertices[0].Position = vec3(0.0f, halfHeight, 0.0f);
	data.Vertices[0].Normal = vec3(0.0f, 1.0f, 0.0f);
	data.Vertices[0].TexCoord = vec2(0.25f, 0.25f);

	size_t offset = size_t(sides) + 1;
	data.Vertices[offset].Position = vec3(0.0f, -halfHeight, 0.0f);
	data.Vertices[offset].Normal = vec3(0.0f, -1.0f, 0.0f);
	data.Vertices[offset].TexCoord = vec2(0.75f, 0.25f);

	size_t doubleOffset = offset * 2;
	size_t trippleOffset = doubleOffset + size_t(sides);
	for (size_t i = 0; i < sides; i++)
	{
		// TOP CAP VERTICES
		Float32 x = cosf((pi<Float32>() / 2.0f) + (angle * i));
		Float32 z = sinf((pi<Float32>() / 2.0f) + (angle * i));
		vec3 pos = normalize(vec3(x, 0.0f, z));
		data.Vertices[i + 1].Position = (pos * radius) + vec3(0.0f, halfHeight, 0.0f);
		data.Vertices[i + 1].Normal = vec3(0.0f, 1.0f, 0.0f);
		data.Vertices[i + 1].TexCoord = vec2(x + 1.0f, z + 1.0f) * 0.25f;

		// BOTTOM CAP VERTICES
		data.Vertices[offset + i + 1].Position = data.Vertices[i + 1].Position - vec3(0.0f, height, 0.0f);
		data.Vertices[offset + i + 1].Normal = vec3(0.0f, -1.0f, 0.0f);
		data.Vertices[offset + i + 1].TexCoord = data.Vertices[i + 1].TexCoord + vec2(0.5f, 0.5f);
			 
		// TOP SIDE VERTICES
		data.Vertices[doubleOffset + i].Position = data.Vertices[i + 1].Position;
		data.Vertices[doubleOffset + i].Normal = pos;
		data.Vertices[doubleOffset + i].TexCoord = vec2(0.0f + (uOffset * i), 1.0f);

		// BOTTOM SIDE VERTICES
		data.Vertices[trippleOffset + i].Position = data.Vertices[offset + i + 1].Position;
		data.Vertices[trippleOffset + i].Normal = pos;
		data.Vertices[trippleOffset + i].TexCoord = vec2(0.0f + (uOffset * i), 0.25f);
	}

	// TOP CAP INDICES
	size_t index = 0;
	for (uint32 i = 0; i < sides; i++)
	{
		data.Indices[index + 0] = i + 1;
		data.Indices[index + 1] = (i + 1) % (sides)+1;
		data.Indices[index + 2] = 0;
		index += 3;
	}

	// BOTTOM CAP INDICES
	for (uint32 i = 0; i < sides; i++)
	{
		uint32 base = uint32(sides) + 1;
		data.Indices[index + 0] = base + ((i + 1) % (sides)+1);
		data.Indices[index + 1] = base + i + 1;
		data.Indices[index + 2] = base;
		index += 3;
	}

	// SIDES
	for (uint32 i = 0; i < sides; i++)
	{
		uint32 base = (uint32(sides) + 1) * 2;
		data.Indices[index + 0] = base + i + 1;
		data.Indices[index + 1] = base + i;
		data.Indices[index + 2] = base + i + sides;
		data.Indices[index + 3] = base + ((i + 1) % sides);
		data.Indices[index + 4] = (base + sides - 1) + ((i + 1) % sides);
		data.Indices[index + 5] = (base + sides) + ((i + 1) % sides);
		index += 6;
	}

	CalculateTangents(data);
	return data;
	*/
	return MeshData();
}

void MeshFactory::Subdivide(MeshData& OutData, Uint32 Subdivisions) noexcept
{	
	if (Subdivisions < 1)
	{
		return;
	}

	Vertex TempVertices[3];
	Uint32 IndexCount		= 0;
	Uint32 VertexCount		= 0;
	Uint32 OldVertexCount	= 0;
	OutData.Vertices.reserve((OutData.Vertices.size() * static_cast<size_t>(pow(2, Subdivisions))));
	OutData.Indices.reserve((OutData.Indices.size() * static_cast<size_t>(pow(4, Subdivisions))));

	for (Uint32 i = 0; i < Subdivisions; i++)
	{
		OldVertexCount	= Uint32(OutData.Vertices.size());
		IndexCount		= Uint32(OutData.Indices.size());

		for (Uint32 j = 0; j < IndexCount; j += 3)
		{
			// Calculate Position
			XMVECTOR Position0 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j    ]].Position);
			XMVECTOR Position1 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j + 1]].Position);
			XMVECTOR Position2 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j + 2]].Position);

			XMVECTOR Position = XMVectorAdd(Position0, Position1);
			Position = XMVectorScale(Position, 0.5f);
			XMStoreFloat3(&TempVertices[0].Position, Position);

			Position = XMVectorAdd(Position0, Position2);
			Position = XMVectorScale(Position, 0.5f);
			XMStoreFloat3(&TempVertices[1].Position, Position);

			Position = XMVectorAdd(Position1, Position2);
			Position = XMVectorScale(Position, 0.5f);
			XMStoreFloat3(&TempVertices[2].Position, Position);
			
			// Calculate TexCoord
			XMVECTOR TexCoord0 = XMLoadFloat2(&OutData.Vertices[OutData.Indices[j]].TexCoord);
			XMVECTOR TexCoord1 = XMLoadFloat2(&OutData.Vertices[OutData.Indices[j + 1]].TexCoord);
			XMVECTOR TexCoord2 = XMLoadFloat2(&OutData.Vertices[OutData.Indices[j + 2]].TexCoord);

			XMVECTOR TexCoord = XMVectorAdd(TexCoord0, TexCoord1);
			TexCoord = XMVectorScale(TexCoord, 0.5f);
			XMStoreFloat2(&TempVertices[0].TexCoord, TexCoord);

			TexCoord = XMVectorAdd(TexCoord0, TexCoord2);
			TexCoord = XMVectorScale(TexCoord, 0.5f);
			XMStoreFloat2(&TempVertices[1].TexCoord, TexCoord);

			TexCoord = XMVectorAdd(TexCoord1, TexCoord2);
			TexCoord = XMVectorScale(TexCoord, 0.5f);
			XMStoreFloat2(&TempVertices[2].TexCoord, TexCoord);

			// Calculate Normal
			XMVECTOR Normal0 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j]].Normal);
			XMVECTOR Normal1 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j + 1]].Normal);
			XMVECTOR Normal2 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j + 2]].Normal);

			XMVECTOR Normal = XMVectorAdd(Normal0, Normal1);
			Normal = XMVectorScale(Normal, 0.5f);
			Normal = XMVector3Normalize(Normal);
			XMStoreFloat3(&TempVertices[0].Normal, Normal);

			Normal = XMVectorAdd(Normal0, Normal2);
			Normal = XMVectorScale(Normal, 0.5f);
			Normal = XMVector3Normalize(Normal);
			XMStoreFloat3(&TempVertices[1].Normal, Normal);

			Normal = XMVectorAdd(Normal1, Normal2);
			Normal = XMVectorScale(Normal, 0.5f);
			Normal = XMVector3Normalize(Normal);
			XMStoreFloat3(&TempVertices[2].Normal, Normal);

			// Calculate Tangent
			XMVECTOR Tangent0 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j]].Tangent);
			XMVECTOR Tangent1 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j + 1]].Tangent);
			XMVECTOR Tangent2 = XMLoadFloat3(&OutData.Vertices[OutData.Indices[j + 2]].Tangent);

			XMVECTOR Tangent = XMVectorAdd(Tangent0, Tangent1);
			Tangent = XMVectorScale(Tangent, 0.5f);
			Tangent = XMVector3Normalize(Tangent);
			XMStoreFloat3(&TempVertices[0].Tangent, Tangent);

			Tangent = XMVectorAdd(Tangent0, Tangent2);
			Tangent = XMVectorScale(Tangent, 0.5f);
			Tangent = XMVector3Normalize(Tangent);
			XMStoreFloat3(&TempVertices[1].Tangent, Tangent);

			Tangent = XMVectorAdd(Tangent1, Tangent2);
			Tangent = XMVectorScale(Tangent, 0.5f);
			Tangent = XMVector3Normalize(Tangent);
			XMStoreFloat3(&TempVertices[2].Tangent, Tangent);

			// Push the new Vertices
			OutData.Vertices.emplace_back(TempVertices[0]);
			OutData.Vertices.emplace_back(TempVertices[1]);
			OutData.Vertices.emplace_back(TempVertices[2]);

			// Push index of the new triangles
			VertexCount = Uint32(OutData.Vertices.size());
			OutData.Indices.emplace_back(VertexCount - 3);
			OutData.Indices.emplace_back(VertexCount - 1);
			OutData.Indices.emplace_back(VertexCount - 2);

			OutData.Indices.emplace_back(VertexCount - 3);
			OutData.Indices.emplace_back(OutData.Indices[j + 1]);
			OutData.Indices.emplace_back(VertexCount - 1);

			OutData.Indices.emplace_back(VertexCount - 2);
			OutData.Indices.emplace_back(VertexCount - 1);
			OutData.Indices.emplace_back(OutData.Indices[j + 2]);

			// Reassign the old indexes
			OutData.Indices[j + 1] = VertexCount - 3;
			OutData.Indices[j + 2] = VertexCount - 2;
		}

		Optimize(OutData, OldVertexCount);
	}

	OutData.Vertices.shrink_to_fit();
	OutData.Indices.shrink_to_fit();
}

void MeshFactory::Optimize(MeshData& OutData, Uint32 StartVertex) noexcept
{
	Uint32 VertexCount	= static_cast<Uint32>(OutData.Vertices.size());
	Uint32 IndexCount	= static_cast<Uint32>(OutData.Indices.size());
		
	Uint32 k = 0;
	Uint32 j = 0;

	for (Uint32 i = StartVertex; i < VertexCount; i++)
	{
		for (j = 0; j < VertexCount; j++)
		{
			if (OutData.Vertices[i] == OutData.Vertices[j])
			{
				if (i != j)
				{
					OutData.Vertices.erase(OutData.Vertices.begin() + i);
					VertexCount--;
					j--;

					for (k = 0; k < IndexCount; k++)
					{
						if (OutData.Indices[k] == i)
						{
							OutData.Indices[k] = j;
						}
						else if (OutData.Indices[k] > i)
						{
							OutData.Indices[k]--;
						}
					}

					i--;
					break;
				}
			}
		}
	}
}

void MeshFactory::CalculateHardNormals(MeshData& data) noexcept
{
	/*
	vec3 e1;
	vec3 e2;
	vec3 n;

	for (size_t i = 0; i < data.Indices.size(); i += 3)
	{
		e1 = data.Vertices[data.Indices[i + 2]].Position - data.Vertices[data.Indices[i]].Position;
		e2 = data.Vertices[data.Indices[i + 1]].Position - data.Vertices[data.Indices[i]].Position;
		n = cross(e1, e2);

		data.Vertices[data.Indices[i]].Normal = n;
		data.Vertices[data.Indices[i + 1]].Normal = n;
		data.Vertices[data.Indices[i + 2]].Normal = n;
	}
	*/
}

void MeshFactory::CalculateTangents(MeshData& data) noexcept
{
	/*
	for (uint32 i = 0; i < data.Indices.size(); i += 3)
	{
		vec3 modelVec = data.Vertices[data.Indices[i + 1]].Position - data.Vertices[data.Indices[i]].Position;
		vec3 modelVec2 = data.Vertices[data.Indices[i + 2]].Position - data.Vertices[data.Indices[i]].Position;
		vec2 tangentVec = data.Vertices[data.Indices[i + 1]].TexCoord - data.Vertices[data.Indices[i]].TexCoord;
		vec2 tangentVec2 = data.Vertices[data.Indices[i + 2]].TexCoord - data.Vertices[data.Indices[i]].TexCoord;

		float denominator = 1.0f / ((tangentVec.x * tangentVec2.y) - (tangentVec2.x * tangentVec.y));
		vec3 tangent = normalize(((modelVec * tangentVec2.y) - (modelVec2 * tangentVec.y)) * denominator);

		data.Vertices[data.Indices[i + 0]].Tangent = tangent;
		data.Vertices[data.Indices[i + 1]].Tangent = tangent;
		data.Vertices[data.Indices[i + 2]].Tangent = tangent;
	}
	*/
}

/*void Mesh::calcNormal()
{
	using namespace Math;

	for (uint32 i = 0; i < indexBuffer->size(); i += 3)
	{
		vec3 edge1 = (*vertexBuffer)[(*indexBuffer)[i + 2]].Position - (*vertexBuffer)[(*indexBuffer)[i + 1]].Position;
		vec3 edge2 = (*vertexBuffer)[(*indexBuffer)[i]].Position - (*vertexBuffer)[(*indexBuffer)[i + 2]].Position;
		vec3 edge3 = (*vertexBuffer)[(*indexBuffer)[i + 1]].Position - (*vertexBuffer)[(*indexBuffer)[i + 0]].Position;

		vec3 Normal = edge1.Cross(edge2);
		Normal.Normalize();

		(*vertexBuffer)[(*indexBuffer)[i]].Normal = Normal;
		(*vertexBuffer)[(*indexBuffer)[i + 1]].Normal = Normal;
		(*vertexBuffer)[(*indexBuffer)[i + 2]].Normal = Normal;
	}
}

void Mesh::calcTangent()
{
	using namespace Math;

	for (uint32 i = 0; i < indexBuffer->size(); i += 3)
	{
		vec3 modelVec = (*vertexBuffer)[(*indexBuffer)[i + 1]].Position - (*vertexBuffer)[(*indexBuffer)[i]].Position;
		vec3 modelVec2 = (*vertexBuffer)[(*indexBuffer)[i + 2]].Position - (*vertexBuffer)[(*indexBuffer)[i]].Position;
		vec2 tangentVec = (*vertexBuffer)[(*indexBuffer)[i + 1]].TexCoord - (*vertexBuffer)[(*indexBuffer)[i]].TexCoord;
		vec2 tangentVec2 = (*vertexBuffer)[(*indexBuffer)[i + 2]].TexCoord - (*vertexBuffer)[(*indexBuffer)[i]].TexCoord;

		float denominator = 1.0f / ((tangentVec.x * tangentVec2.y) - (tangentVec2.x * tangentVec.y));
		vec3 tangent = ((modelVec * tangentVec2.y) - (modelVec2 * tangentVec.y)) * denominator;
		tangent.Normalize();

		(*vertexBuffer)[(*indexBuffer)[i]].tangent = tangent;
		(*vertexBuffer)[(*indexBuffer)[i + 1]].tangent = tangent;
		(*vertexBuffer)[(*indexBuffer)[i + 2]].tangent = tangent;
	}
}*/
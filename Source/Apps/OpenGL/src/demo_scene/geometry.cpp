#include "geometry.h"
#include <gli/gli.hpp>
#include <chrono>

inline void Mesh::setupMesh()
{
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glBindVertexArray(0);
}

void Mesh::drawMesh()
{
    bindMesh();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
    unbindMesh();
}

void Mesh::bindMesh()
{
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

    // Vertex Positions
    glEnableVertexAttribArray(POS_ATTRIB);
    glVertexAttribPointer(POS_ATTRIB, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, position));
    // Vertex Normals
    glEnableVertexAttribArray(NORMAL_ATTRIB);
    glVertexAttribPointer(NORMAL_ATTRIB, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, normal));
    // Vertex Texture Coords
    glEnableVertexAttribArray(TEXTURE_ATTRIB);
    glVertexAttribPointer(TEXTURE_ATTRIB, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, texCoords));

    glEnableVertexAttribArray(TANGENT_ATTRIB);
    glVertexAttribPointer(TANGENT_ATTRIB, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, tangent));

    glEnableVertexAttribArray(BITANGENT_ATTRIB);
    glVertexAttribPointer(BITANGENT_ATTRIB, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, bitangent));
}

void Mesh::unbindMesh()
{
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(POS_ATTRIB);
    glDisableVertexAttribArray(NORMAL_ATTRIB);
    glDisableVertexAttribArray(TEXTURE_ATTRIB);
    glDisableVertexAttribArray(TANGENT_ATTRIB);
    glDisableVertexAttribArray(BITANGENT_ATTRIB);
}

Model::Model(const std::string & file)
    : m_path(GetAssetFullPath(file))
    , m_directory(splitFilename(m_path))
{
    loadModel();
}

inline std::string Model::splitFilename(const std::string & str)
{
    return str.substr(0, str.find_last_of("/"));
}

inline void Model::loadModel()
{
    auto start = std::chrono::system_clock::now();
    Assimp::Importer import;
    const aiScene* scene = import.ReadFile(m_path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_OptimizeMeshes | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace);
    if (!scene || scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        DBG("ERROR::ASSIMP:: %s", import.GetErrorString());
        return;
    }

    processNode(scene->mRootNode, scene);

    auto duration = std::chrono::duration_cast< std::chrono::milliseconds>(std::chrono::system_clock::now() - start);
    DBG("%lld", duration.count());
}

inline void Model::processNode(aiNode * node, const aiScene * scene)
{
    for (GLuint i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(processMesh(mesh, scene));
        //if (meshes.back().material.name == "16___Default")
        //    meshes.pop_back();
    }

    for (GLuint i = 0; i < node->mNumChildren; i++)
    {
        processNode(node->mChildren[i], scene);
    }
}

inline glm::vec4 Model::aiColor4DToVec4(const aiColor4D & x)
{
    return glm::vec4(x.r, x.g, x.b, x.a);
}

inline Mesh Model::processMesh(aiMesh * mesh, const aiScene * scene)
{
    Mesh retMeh;
    // Walk through each of the mesh's vertices
    for (GLuint i = 0; i < mesh->mNumVertices; ++i)
    {
        Mesh::Vertex vertex;

        if (mesh->HasPositions())
        {
            vertex.position.x = mesh->mVertices[i].x;
            vertex.position.y = mesh->mVertices[i].y;
            vertex.position.z = mesh->mVertices[i].z;
        }

        if (mesh->HasNormals())
        {
            vertex.normal.x = mesh->mNormals[i].x;
            vertex.normal.y = mesh->mNormals[i].y;
            vertex.normal.z = mesh->mNormals[i].z;
        }

        if (mesh->HasTangentsAndBitangents())
        {
            vertex.tangent.x = mesh->mTangents[i].x;
            vertex.tangent.y = mesh->mTangents[i].y;
            vertex.tangent.z = mesh->mTangents[i].z;

            vertex.bitangent.x = mesh->mBitangents[i].x;
            vertex.bitangent.y = mesh->mBitangents[i].y;
            vertex.bitangent.z = mesh->mBitangents[i].z;

            if (glm::dot(glm::cross(vertex.normal, vertex.tangent), vertex.bitangent) < 0.0f)
            {
                vertex.tangent *= -1.0f;
            }
        }

        if (mesh->HasTextureCoords(0))
        {
            // A vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't
            // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
            vertex.texCoords.x = mesh->mTextureCoords[0][i].x;
            vertex.texCoords.y = mesh->mTextureCoords[0][i].y;
        }
        else
            vertex.texCoords = glm::vec2(0.0f, 0.0f);

        retMeh.vertices.push_back(vertex);
    }
    // Now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
    for (GLuint i = 0; i < mesh->mNumFaces; ++i)
    {
        aiFace face = mesh->mFaces[i];
        // Retrieve all indices of the face and store them in the indices vector
        for (GLuint j = 0; j < face.mNumIndices; ++j)
        {
            retMeh.indices.push_back(face.mIndices[j]);
        }
    }

    // Process materials
    if (mesh->mMaterialIndex >= 0)
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        loadMaterialTextures(retMeh, material, aiTextureType_AMBIENT);
        loadMaterialTextures(retMeh, material, aiTextureType_DIFFUSE);
        loadMaterialTextures(retMeh, material, aiTextureType_SPECULAR);
        loadMaterialTextures(retMeh, material, aiTextureType_HEIGHT);
        loadMaterialTextures(retMeh, material, aiTextureType_OPACITY);

        std::vector<Mesh::Texture> add;

        std::pair<std::string, aiTextureType> map_from[] = {
            { "_diff", aiTextureType_DIFFUSE },
            { "_color", aiTextureType_DIFFUSE }
        };

        std::pair<std::string, aiTextureType> map_to[] = {
            { "_spec", aiTextureType_SPECULAR },
            { "_nmap", aiTextureType_HEIGHT }
        };

        for (auto &from_type : map_from)
        {
            for (auto &tex : retMeh.textures)
            {
                if (from_type.second != tex.type)
                    continue;

                for (auto &to_type : map_to)
                {
                    std::string path = tex.path.C_Str();
                    size_t loc = path.find(from_type.first);
                    if (loc == std::string::npos)
                        continue;

                    path.replace(loc, from_type.first.size(), to_type.first);
                    if (!std::ifstream(path).good())
                        continue;

                    Mesh::Texture texture;
                    texture.id = -1;
                    texture.type = to_type.second;
                    texture.path = path;
                    TextureFromFile(texture);
                    add.push_back(texture);
                }
            }
        }
        retMeh.textures.insert(retMeh.textures.end(), add.begin(), add.end());

        aiColor4D amb;
        aiColor4D dif;
        aiColor4D spec;
        float shininess;
        aiString name;

        if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
        {
            retMeh.material.shininess = shininess;
        }
        if (material->Get(AI_MATKEY_COLOR_AMBIENT, amb) == aiReturn_SUCCESS)
        {
            retMeh.material.amb = glm::vec3(aiColor4DToVec4(amb));
        }
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, dif) == aiReturn_SUCCESS)
        {
            retMeh.material.dif = glm::vec3(aiColor4DToVec4(dif));
        }
        if (material->Get(AI_MATKEY_COLOR_SPECULAR, spec) == aiReturn_SUCCESS)
        {
            retMeh.material.spec = glm::vec3(aiColor4DToVec4(spec));
        }
        if (material->Get(AI_MATKEY_NAME, name) == aiReturn_SUCCESS)
        {
            retMeh.material.name = name.C_Str();
        }
    }

    retMeh.setupMesh();
    return retMeh;
}

inline void Model::loadMaterialTextures(Mesh & retMeh, aiMaterial * mat, aiTextureType type)
{
    for (uint32_t i = 0; i < mat->GetTextureCount(type); ++i)
    {
        aiString texture_name;
        mat->GetTexture(type, i, &texture_name);
        std::string texture_path = m_directory + "/" + texture_name.C_Str();
        if (!std::ifstream(texture_path).good())
        {
            DBG("failed to load %s", texture_path.c_str());
            continue;
        }

        Mesh::Texture texture;
        texture.id = -1;
        texture.type = type;
        texture.path = texture_path;
        TextureFromFile(texture);
        retMeh.textures.push_back(texture);
    }
}


GLuint CreateCompressedTexture(const std::string& Filename, aiTextureType type)
{
    gli::texture Texture = gli::load(Filename);
    if (Texture.empty())
        return 0;

    gli::gl GL(gli::gl::PROFILE_GL33);
    gli::gl::format const Format = GL.translate(Texture.format(), Texture.swizzles());
    GLenum Target = GL.translate(Texture.target());
    //assert(gli::is_compressed(Texture.format()) && Target == gli::TARGET_2D);

    GLuint TextureName = 0;
    glGenTextures(1, &TextureName);
    glBindTexture(Target, TextureName);
    glTexParameteri(Target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(Target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(Texture.levels() - 1));
    glTexParameteriv(Target, GL_TEXTURE_SWIZZLE_RGBA, &Format.Swizzles[0]);
    glTexStorage2D(Target, static_cast<GLint>(Texture.levels()), Format.Internal, Texture.extent(0).x, Texture.extent(0).y);
    for (std::size_t Level = 0; Level < Texture.levels(); ++Level)
    {
        glm::tvec3<GLsizei> Extent(Texture.extent(Level));
        glCompressedTexSubImage2D(Target, static_cast<GLint>(Level), 0, 0, Extent.x, Extent.y,
            Format.Internal, static_cast<GLsizei>(Texture.size(Level)), Texture.data(0, 0, Level));
    }


    if (type == aiTextureType_OPACITY)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)GL_NEAREST);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)GL_REPEAT);
    glBindTexture(Target, 0);

    return TextureName;
}

inline void Model::TextureFromFile(Mesh::Texture & texture)
{
    DBG("texture %s", texture.path.C_Str());
    texture.id = CreateCompressedTexture(texture.path.C_Str(), texture.type);
}
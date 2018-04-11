#pragma once
class CD3D1XShader;
class CD3D1XVertexDeclaration;
class CD3D1XVertexBuffer;
class CD3D1XIndexBuffer;
struct QuadVertex
{
	RwV4d Position;
	RwV2d TexCoord;
};
class CFullscreenQuad
{
public:
	static void Init();
	static void Shutdown();
	static void Draw();
private:
	static CD3D1XVertexBuffer* m_quadVB;
	static CD3D1XIndexBuffer* m_quadIB;
	static CD3D1XShader* m_quadVS;
	static CD3D1XVertexDeclaration* m_pVertexDecl;
};


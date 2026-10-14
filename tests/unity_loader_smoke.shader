Shader "FXC2/Smoke" { SubShader { Pass { HLSLPROGRAM
#pragma only_renderers d3d11
#pragma vertex vert
#pragma fragment frag
#pragma target 5.0
float4 vert(float4 p : POSITION) : SV_POSITION { return p; }
float4 frag(float4 p : SV_POSITION) : SV_Target { return float4((float)mulhi((uint)p.x, 0xfffffffdu), 0, 0, 1); }
ENDHLSL } } }

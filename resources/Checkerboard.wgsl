@group(0) @binding(0) var<uniform> uTime: f32;

struct VertexInput {
	@location(0) position: vec2f,
	@location(1) texCoord: vec2f,
};

struct VertexOutput {
	@builtin(position) position: vec4f,
	@location(0) texCoord: vec2f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	out.position = vec4f(in.position, 0.0, 1.0);
	out.texCoord = in.texCoord;
	return out;
}

// from https://iquilezles.org/articles/checkerfiltering/
fn checker(a : vec2f) -> f32 {
	//return a.x;

	var q : vec2f = vec2f(floor(a.x), floor(a.y));
	return (q.x + q.y) % 2.0;

	//var s : vec2f = vec2f(sign(fract(a * .5) - .5));
	//return .5 - .5 * s.x * s.y;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
	var checkerVal : f32 = checker(in.texCoord * 20.0);
	return vec4f(checkerVal, checkerVal, checkerVal, 1.0);
    //return vec4f(in.texCoord.x, in.texCoord.y, 0.5 + sin(uTime) * 0.5, 1.0);
}
import struct, math, numpy as np, pygame, tkinter as tk
from tkinter import filedialog
from dataclasses import dataclass

# ─────────────—— Track parsing (unchanged) ─────────────——
@dataclass
class Curve:
	points: list
	def sample(self, t: float) -> float:
		if not self.points: return 0.0
		if len(self.points) == 1 or t <= self.points[0][0]: return self.points[0][1]
		if t >= self.points[-1][0]: return self.points[-1][1]
		i = 0
		pts = self.points
		while i + 1 < len(pts) and t > pts[i + 1][0]:
			i += 1
		t0, v0, tan_in0, tan_out0 = pts[i]
		t1, v1, tan_in1, tan_out1 = pts[i + 1]
		dt = t1 - t0
		if dt == 0: return v1
		u = (t - t0) / dt
		p0_handle = v0 + (dt/3.0)*tan_out0
		p1_handle = v1 - (dt/3.0)*tan_in1
		omt = 1.0 - u
		return omt**3*v0 + 3*omt*omt*u*p0_handle + 3*omt*u*u*p1_handle + u**3*v1

def _u32(f): return struct.unpack("<I", f.read(4))[0]
def _f32(f): return struct.unpack("<f", f.read(4))[0]
def _vec3(f): return struct.unpack("<3f", f.read(12))
def _curve(f):
	c = _u32(f)
	return Curve([struct.unpack("<4f", f.read(16)) for _ in range(c)])

def parse_track(path):
	with open(path, "rb") as f:
		header_size = _u32(f)
		version = f.read(4).decode("ascii")

		cp_count = _u32(f)
		seg_count = _u32(f)
		trig_count = _u32(f) if version not in ("v0.1", "v0.2") else 0

		for _ in range(cp_count):
			_vec3(f)				# position
			_vec3(f)				# normal
			f.read(36)				# bbox min/max
			f.read(36)
			f.read(16)				# spline params
			f.read(12)				# colour
			_u32(f)					# flags
			f.read(12)				# something
			f.read(4)				# u32
			f.read(12)				# something
			f.read(4)				# u32
			conn_count = _u32(f)	# connected CP indices
			f.read(conn_count * 4)

		segments = []
		for _ in range(seg_count):
			index      = _u32(f)
			road_type  = _u32(f)
			openness   = _curve(f) if road_type in (2, 4) else None

			modulations=[(_curve(f), _curve(f)) for _ in range(_u32(f))]

			for _ in range(_u32(f)):		# embeddings
				f.read(8); _u32(f)
				_curve(f); _curve(f)

			# 15 curves: 3 pos, 9 rot, 3 scale
			curves=[_curve(f) for _ in range(15)]
			f.read(8)	# rails

			segments.append({
				"index": index,
				"shape_type": road_type,
				"openness": openness,
				"modulations": modulations,
				"curves": curves,
			})
		return segments


# ─────────────—— Geometry sampling (unchanged logic) ─────────────——
def _sample(seg, t):
	cv=seg["curves"]
	pos=(cv[0].sample(t),cv[1].sample(t),cv[2].sample(t))
	basis=[[cv[3+i].sample(t) for i in (0,3,6)],
		[cv[4+i].sample(t) for i in (0,3,6)],
		[cv[5+i].sample(t) for i in (0,3,6)]]
	scale=(cv[12].sample(t),cv[13].sample(t),cv[14].sample(t))
	return pos,basis,scale

def _xform(p,b,s,l):
	lx,ly,lz=l[0]*s[0],l[1]*s[1],l[2]*s[2]
	return (p[0]+b[0][0]*lx+b[0][1]*ly+b[0][2]*lz,
		p[1]+b[1][0]*lx+b[1][1]*ly+b[1][2]*lz,
		p[2]+b[2][0]*lx+b[2][1]*ly+b[2][2]*lz)

def _surf(seg,t,slices=25):
	p,b,s=_sample(seg,t)
	shape=seg["shape_type"]
	openv=seg["openness"].sample(t) if seg["openness"] else 1.0
	mods=seg["modulations"]
	pts=[]
	for i in range(slices):
		x=-1.0+2.0*(i/(slices-1))
		if shape==1: m_t=1-(x+1)*0.5; ang=(x-0.5)*math.pi; d=pygame.math.Vector3(math.sin(ang),math.cos(ang),0).normalize()
		elif shape==2: m_t=1-(x+1)*0.5; tx=x*openv; ang=tx*math.pi; d=pygame.math.Vector3(math.sin(ang),math.cos(ang),0).normalize()
		elif shape==3: m_t=(x+1)*0.5; ang=(x-0.5)*math.pi; d=pygame.math.Vector3(math.cos(ang),math.sin(ang),0).normalize()
		elif shape==4: m_t=1-(x+1)*0.5; tx=x*openv; ang=(tx-0.5)*math.pi; d=pygame.math.Vector3(math.cos(ang),math.sin(ang),0).normalize()
		else: m_t=0.5*(1-x)
		off=0.0
		for eff,hgt in mods:
			a=eff.sample(t)
			if a!=0: off+=hgt.sample(m_t)*a
		local=(x,off,0) if shape==0 else (d.x*(1+off),d.y*(1+off),0)
		pts.append(_xform(p,b,s,local))
	return pts

def _normal(p0,p1,p2):
	v1=pygame.math.Vector3(*(np.subtract(p1,p0)))
	v2=pygame.math.Vector3(*(np.subtract(p2,p0)))
	n=v2.cross(v1)
	return (0,1,0) if n.length()==0 else n.normalize()

def _shade(n,ld=(0.1,1,0.3)):
	i=max(min(pygame.math.Vector3(*ld).normalize().dot(pygame.math.Vector3(*n))*0.5+0.5,1),0)
	return (i,i,i)

def build_mesh(segs):
	verts=[]
	minv=np.array([1e9,1e9,1e9]); maxv=-minv
	t_vals=[i/127.0 for i in range(128)]
	for sg in segs:
		samp=[_surf(sg,t_vals[k]) for k in range(128)]
		for r in range(127):
			r0,r1=samp[r],samp[r+1]
			for c in range(24):
				p0,p1,p2,p3=r0[c],r0[c+1],r1[c+1],r1[c]
				n=_normal(p0,p1,p2); col=_shade(n)
				for tri in ((p0,p1,p2),(p0,p2,p3)):
					for v in tri:
						verts.extend([v[0],v[1],v[2],*col])
						minv=np.minimum(minv,v); maxv=np.maximum(maxv,v)
	rad=np.max(np.abs(np.concatenate([minv,maxv])))
	cen=(minv+maxv)/2
	vs=np.array(verts,dtype='f4').reshape(-1,6)
	vs[:,:3]-=cen  # center at origin
	return vs.astype('f4').tobytes(),float(rad)

# ─────────────—— Arc-ball helpers ─────────────——
def _map_to_sphere(pos,w,h):
	x=(2*pos[0]-w)/w
	y=(h-2*pos[1])/h
	l2=x*x+y*y
	if l2>1: norm=1/math.sqrt(l2); return np.array([x*norm,y*norm,0],dtype='f4')
	return np.array([x,y,math.sqrt(1-l2)],dtype='f4')

def _rot_matrix(axis,theta):
	ax=axis/np.linalg.norm(axis)
	s,c=np.sin(theta),np.cos(theta)
	x,y,z=ax
	K=np.array([[0,-z,y],[z,0,-x],[-y,x,0]],dtype='f4')
	return np.eye(3,dtype='f4')+s*K+(1-c)*K@K

# ─────────────—— OpenGL init ─────────────——
VERT_SRC="""
#version 330
in vec3 in_pos;
in vec3 in_color;
out vec3 v_color;
uniform mat4 mvp;
void main(){ gl_Position = mvp*vec4(in_pos,1.0); v_color=in_color; }
"""
FRAG_SRC="""
#version 330
in vec3 v_color;
out vec4 f;
void main(){ f=vec4(v_color,1.0); }
"""

def _persp(fov,asp,near,far):
	f=1/math.tan(fov*0.5)
	M=np.zeros((4,4),dtype='f4')
	M[0,0]=f/asp; M[1,1]=f
	M[2,2]=(far+near)/(near-far); M[2,3]=(2*far*near)/(near-far)
	M[3,2]=-1
	return M

def main():
	pygame.init()
	pygame.display.gl_set_attribute(pygame.GL_CONTEXT_MAJOR_VERSION,3)
	pygame.display.gl_set_attribute(pygame.GL_CONTEXT_MINOR_VERSION,3)
	pygame.display.gl_set_attribute(pygame.GL_CONTEXT_PROFILE_MASK,pygame.GL_CONTEXT_PROFILE_CORE)
	w,h=800,800
	pygame.display.set_mode((w,h),pygame.OPENGL|pygame.DOUBLEBUF)
	from moderngl import create_context
	ctx=create_context(); ctx.enable(ctx.DEPTH_TEST)
	prog=ctx.program(vertex_shader=VERT_SRC,fragment_shader=FRAG_SRC)

	root=tk.Tk(); root.withdraw()
	path="B:/programming/mxt-cpp/export-bin/track/anotherone/track.mxt_track"
	data,rad=build_mesh(parse_track(path))
	vbo=ctx.buffer(data); vao=ctx.simple_vertex_array(prog,vbo,'in_pos','in_color')

	dist=rad*2.5
	orient=np.eye(3,dtype='f4')
	drag=False; last_v=None
	clock=pygame.time.Clock()

	def reload(p):
		nonlocal data,rad,vbo,vao,dist
		data,rad=build_mesh(parse_track(p))
		vbo.release(); vao.release()
		vbo=ctx.buffer(data); vao=ctx.simple_vertex_array(prog,vbo,'in_pos','in_color')
		dist=rad*2.5

	while True:
		for ev in pygame.event.get():
			if ev.type==pygame.QUIT: return
			elif ev.type==pygame.KEYDOWN and ev.key==pygame.K_f:
				fn=filedialog.askopenfilename(title="Open .mxt_track",filetypes=[("Track","*.mxt_track")])
				if fn: reload(fn)
			elif ev.type==pygame.DROPFILE: reload(ev.file)
			elif ev.type==pygame.MOUSEBUTTONDOWN and ev.button==3:
				drag=True; last_v=_map_to_sphere(ev.pos,w,h)
			elif ev.type==pygame.MOUSEBUTTONUP and ev.button==3:
				drag=False
			elif ev.type==pygame.MOUSEWHEEL:
				dist*=0.9 if ev.y>0 else 1.1
			elif ev.type==pygame.MOUSEMOTION and drag:
				cur=_map_to_sphere(ev.pos,w,h)
				axis=np.cross(last_v,cur)
				if np.linalg.norm(axis)>1e-5:
					theta=math.acos(max(-1,min(1,np.dot(last_v,cur))))
					orient=_rot_matrix(axis,theta)@orient
				last_v=cur

		ctx.clear(0,0,0,1)
		# MVP
		model=np.eye(4,dtype='f4'); model[:3,:3]=orient
		view=np.eye(4,dtype='f4'); view[2,3]=-dist
		proj=_persp(math.radians(45),w/h,rad*0.01,rad*20)
		mvp = proj @ view @ model
		prog['mvp'].write(mvp.T.astype('f4').tobytes())  # transpose → column-major
		vao.render()
		pygame.display.flip()
		clock.tick(60)

if __name__=="__main__":
	main()

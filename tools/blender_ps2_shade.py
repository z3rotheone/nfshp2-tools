"""Light an imported HP2 model the way the PS2 does.

Run it in Blender after importing an OBJ from hp2_geom.py --obj: open the
Scripting tab, open this file, press Run Script. Then look at the car in
Material Preview.

On the PS2 each pixel is texture x vertex colour / 128. The car's vertex
colours are 5-bit (0..31) and the GS widens them to 8 bits by shifting left
3, so a stored 16 counts as 1.0 and a stored 31 brightens the texture by
31/16. The textures are kept at about half brightness for that reason, which
is why the cars look dull without this step. The vertex colours themselves
carry baked shading, not paint.

For every material whose Principled BSDF takes its Base Color from a
texture (or a flat colour), this puts texture x vertex colour x GAIN in
between, clamped to 1 as the GS clamps to 255. Running it twice does
nothing more: the nodes it adds are tagged and it skips materials that
already have them. It also sets see-through materials (the windows) to
smooth blending instead of Blender's speckled dithering.

Blender works in linear light and turns both the PNGs and the OBJ's vertex
colours from sRGB to linear when it loads them. The PS2 multiplies the sRGB
values, and (a*b*k)^2.2 = a^2.2 * b^2.2 * k^2.2, so in linear the gain is
(31/16)^2.2. If the car comes out far too bright, your Blender kept the
vertex colours as they were; set GAIN to 31/16 and run it on a fresh import.
"""
import bpy

GAIN = (31 / 16) ** 2.2
TAG = "hp2_ps2"


def colour_in(node, name):
    for s in node.inputs:
        if s.name == name and s.type == 'RGBA' and s.enabled:
            return s
    raise LookupError("%s has no colour input %s" % (node.name, name))


def colour_out(node):
    for s in node.outputs:
        if s.type == 'RGBA' and s.enabled:
            return s
    raise LookupError("%s has no colour output" % node.name)


def multiply(nt, clamp):
    n = nt.nodes.new("ShaderNodeMix")
    n.data_type = 'RGBA'
    n.blend_type = 'MULTIPLY'
    n.clamp_result = clamp
    n.inputs[0].default_value = 1.0      # factor
    n[TAG] = 1
    return n


done = skipped = 0
for mat in bpy.data.materials:
    if not mat.use_nodes:
        skipped += 1
        continue
    nt = mat.node_tree
    if any(n.get(TAG) for n in nt.nodes):
        continue
    bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if bsdf is None:
        skipped += 1
        continue
    base = bsdf.inputs["Base Color"]
    src = base.links[0].from_socket if base.is_linked else None
    flat = tuple(base.default_value)

    vc = nt.nodes.new("ShaderNodeVertexColor")   # empty name = active colours
    vc[TAG] = 1
    m1 = multiply(nt, clamp=False)
    m2 = multiply(nt, clamp=True)

    if src is not None:
        nt.links.new(src, colour_in(m1, "A"))
    else:
        colour_in(m1, "A").default_value = flat
    nt.links.new(vc.outputs["Color"], colour_in(m1, "B"))
    nt.links.new(colour_out(m1), colour_in(m2, "A"))
    colour_in(m2, "B").default_value = (GAIN, GAIN, GAIN, 1.0)
    nt.links.new(colour_out(m2), base)

    x, y = bsdf.location
    vc.location = (x - 700, y - 250)
    m1.location = (x - 450, y)
    m2.location = (x - 220, y)
    done += 1

# Glass: the windows draw WINDOW, whose alpha makes them see-through. Blender's
# OBJ import leaves such materials in dithered ("hashed") transparency, which
# shows as speckles; switch them to smooth blending, and wire the texture's
# alpha in if the import did not.
glass = 0
for mat in bpy.data.materials:
    if not mat.use_nodes:
        continue
    nt = mat.node_tree
    bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if bsdf is None:
        continue
    alpha = bsdf.inputs.get("Alpha")
    if alpha is None:
        continue
    if not alpha.is_linked and "WINDOW" in mat.name.upper():
        img = next((n for n in nt.nodes if n.type == 'TEX_IMAGE'), None)
        if img is not None:
            nt.links.new(img.outputs["Alpha"], alpha)
    if not alpha.is_linked:
        continue
    if hasattr(mat, "surface_render_method"):      # Blender 4.2 and later
        mat.surface_render_method = 'BLENDED'
    else:
        mat.blend_method = 'BLEND'
        if hasattr(mat, "shadow_method"):
            mat.shadow_method = 'NONE'
    mat.use_backface_culling = False
    glass += 1

print("hp2_ps2: shaded %d materials, skipped %d without a Principled BSDF; "
      "%d see-through materials set to smooth blending" % (done, skipped, glass))

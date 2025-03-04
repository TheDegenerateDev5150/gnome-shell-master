import Clutter from 'gi://Clutter';
import Cogl from 'gi://Cogl';
import GObject from 'gi://GObject';
import Shell from 'gi://Shell';
import St from 'gi://St';

import * as Params from '../misc/params.js';

export const DEFAULT_FADE_FACTOR = 0.4;
export const VIGNETTE_BRIGHTNESS = 0.5;
export const VIGNETTE_SHARPNESS = 0.7;

const VIGNETTE_DECLARATIONS = '                                              \
uniform float brightness;                                                  \n\
uniform float vignette_sharpness;                                          \n\
float rand(vec2 p) {                                                       \n\
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123);        \n\
}                                                                          \n';

const VIGNETTE_CODE = '                                                      \
cogl_color_out.a = cogl_color_in.a;                                        \n\
cogl_color_out.rgb = vec3(0.0, 0.0, 0.0);                                  \n\
vec2 position = cogl_tex_coord_in[0].xy - 0.5;                             \n\
float t = clamp(length(1.41421 * position), 0.0, 1.0);                     \n\
float pixel_brightness = mix(1.0, 1.0 - vignette_sharpness, t);            \n\
cogl_color_out.a *= 1.0 - pixel_brightness * brightness;                   \n\
float noise_magnitude = (2.0 / ((1.0 - cogl_color_out.a) * 255.0));        \n\
cogl_color_out.a += (rand(position) - 0.5) * noise_magnitude;              \n';


const RadialShaderEffect = GObject.registerClass({
    Properties: {
        'brightness': GObject.ParamSpec.float(
            'brightness', null, null,
            GObject.ParamFlags.READWRITE,
            0, 1, 1),
        'sharpness': GObject.ParamSpec.float(
            'sharpness', null, null,
            GObject.ParamFlags.READWRITE,
            0, 1, 0),
    },
}, class RadialShaderEffect extends Shell.GLSLEffect {
    _init(params) {
        this._brightness = undefined;
        this._sharpness = undefined;

        super._init(params);

        this._brightnessLocation = this.get_uniform_location('brightness');
        this._sharpnessLocation = this.get_uniform_location('vignette_sharpness');

        this.brightness = 1.0;
        this.sharpness = 0.0;
    }

    vfunc_build_pipeline() {
        this.add_glsl_snippet(Cogl.SnippetHook.FRAGMENT,
            VIGNETTE_DECLARATIONS, VIGNETTE_CODE, true);
    }

    get brightness() {
        return this._brightness;
    }

    set brightness(v) {
        if (this._brightness === v)
            return;
        this._brightness = v;
        this.set_uniform_float(this._brightnessLocation,
            1, [this._brightness]);
        this.notify('brightness');
    }

    get sharpness() {
        return this._sharpness;
    }

    set sharpness(v) {
        if (this._sharpness === v)
            return;
        this._sharpness = v;
        this.set_uniform_float(this._sharpnessLocation,
            1, [this._sharpness]);
        this.notify('sharpness');
    }
});

/**
 * Lightbox:
 */
export const Lightbox = GObject.registerClass({
    Properties: {
        'active': GObject.ParamSpec.boolean(
            'active', null, null, GObject.ParamFlags.READABLE, false),
    },
}, class Lightbox extends St.Bin {
    /**
     * Lightbox creates a dark translucent "shade" actor to hide the
     * contents of `container`, and allows you to specify particular actors
     * in `container` to highlight by bringing them above the shade. It
     * tracks added and removed actors in `container` while the lightboxing
     * is active, and ensures that all actors are returned to their
     * original stacking order when the lightboxing is removed. (However,
     * if actors are restacked by outside code while the lightboxing is
     * active, the lightbox may later revert them back to their original
     * order.)
     *
     * By default, the shade window will have the height and width of
     * `container` and will track any changes in its size. You can override
     * this by passing an explicit width and height in `params`.
     *
     * @param {Clutter.Container} container parent Clutter.Container
     * @param {object} [params] additional parameters:
     * @param {boolean=} params.inhibitEvents: whether to inhibit events for `container`
     * @param {number=} params.width: shade actor width
     * @param {number=} params.height: shade actor height
     * @param {number=} params.fadeFactor: fading opacity factor
     * @param {boolean=} params.radialEffect: whether to enable the GLSL radial effect
     */
    _init(container, params) {
        params = Params.parse(params, {
            inhibitEvents: false,
            width: null,
            height: null,
            fadeFactor: DEFAULT_FADE_FACTOR,
            radialEffect: false,
        });

        super._init({
            reactive: params.inhibitEvents,
            width: params.width,
            height: params.height,
            visible: false,
        });

        this._active = false;
        this._container = container;
        this._children = container.get_children();
        this._fadeFactor = params.fadeFactor;
        this._radialEffect = params.radialEffect;

        if (this._radialEffect)
            this.add_effect(new RadialShaderEffect({name: 'radial'}));
        else
            this.set({opacity: 0, style_class: 'lightbox'});

        container.add_child(this);
        container.set_child_above_sibling(this, null);

        this.connect('destroy', this._onDestroy.bind(this));

        if (!params.width || !params.height) {
            this.add_constraint(new Clutter.BindConstraint({
                source: container,
                coordinate: Clutter.BindCoordinate.ALL,
            }));
        }

        container.connectObject(
            'child-added', this._childAdded.bind(this),
            'child-removed', this._childRemoved.bind(this), this);

        this._highlighted = null;
    }

    get active() {
        return this._active;
    }

    _childAdded(container, newChild) {
        let children = this._container.get_children();
        let myIndex = children.indexOf(this);
        let newChildIndex = children.indexOf(newChild);

        if (newChildIndex > myIndex) {
            // The child was added above the shade (presumably it was
            // made the new top-most child). Move it below the shade,
            // and add it to this._children as the new topmost actor.
            this._container.set_child_above_sibling(this, newChild);
            this._children.push(newChild);
        } else if (newChildIndex === 0) {
            // Bottom of stack
            this._children.unshift(newChild);
        } else {
            // Somewhere else; insert it into the correct spot
            let prevChild = this._children.indexOf(children[newChildIndex - 1]);
            if (prevChild !== -1) // paranoia
                this._children.splice(prevChild + 1, 0, newChild);
        }
    }

    lightOn(fadeInTime) {
        this.remove_all_transitions();

        let easeProps = {
            duration: fadeInTime || 0,
            mode: Clutter.AnimationMode.EASE_OUT_QUAD,
        };

        let onComplete = () => {
            this._active = true;
            this.notify('active');
        };

        this.show();

        if (this._radialEffect) {
            this.ease_property(
                '@effects.radial.brightness', VIGNETTE_BRIGHTNESS, easeProps);
            this.ease_property(
                '@effects.radial.sharpness', VIGNETTE_SHARPNESS,
                {onComplete, ...easeProps});
        } else {
            this.ease({
                ...easeProps,
                opacity: 255 * this._fadeFactor,
                onComplete,
            });
        }
    }

    lightOff(fadeOutTime) {
        this.remove_all_transitions();

        this._active = false;
        this.notify('active');

        let easeProps = {
            duration: fadeOutTime || 0,
            mode: Clutter.AnimationMode.EASE_OUT_QUAD,
        };

        let onComplete = () => this.hide();

        if (this._radialEffect) {
            this.ease_property(
                '@effects.radial.brightness', 1.0, easeProps);
            this.ease_property(
                '@effects.radial.sharpness', 0.0, {onComplete, ...easeProps});
        } else {
            this.ease({...easeProps, opacity: 0, onComplete});
        }
    }

    _childRemoved(container, child) {
        let index = this._children.indexOf(child);
        if (index !== -1) // paranoia
            this._children.splice(index, 1);

        if (child === this._highlighted)
            this._highlighted = null;
    }

    /**
     * highlight:
     *
     * Highlights the indicated actor and unhighlights any other
     * currently-highlighted actor. With no arguments or a false/null
     * argument, all actors will be unhighlighted.
     *
     * @param {Clutter.Actor=} window actor to highlight
     */
    highlight(window) {
        if (this._highlighted === window)
            return;

        // Walk this._children raising and lowering actors as needed.
        // Things get a little tricky if the to-be-raised and
        // to-be-lowered actors were originally adjacent, in which
        // case we may need to indicate some *other* actor as the new
        // sibling of the to-be-lowered one.

        let below = this;
        for (let i = this._children.length - 1; i >= 0; i--) {
            if (this._children[i] === window)
                this._container.set_child_above_sibling(this._children[i], null);
            else if (this._children[i] === this._highlighted)
                this._container.set_child_below_sibling(this._children[i], below);
            else
                below = this._children[i];
        }

        this._highlighted = window;
    }

    /**
     * _onDestroy:
     *
     * This is called when the lightbox' actor is destroyed, either
     * by destroying its container or by explicitly calling this.destroy().
     */
    _onDestroy() {
        this.highlight(null);
    }
});

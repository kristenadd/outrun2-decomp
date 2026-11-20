// visionOS application: one resizable window showing the game in stereoscopic
// 3D. The game (C++) runs on its own thread and composes both eyes of every
// frame (vision_bridge.h); here a RealityKit plane fills the window with a
// material that shows each eye its own texture (StereoMaterial.usda). The
// window keeps the aspect the game asks for (Options > Settings > ASPECT
// RATIO: 4:3 or 16:9) while the player resizes it. There is no full-screen
// (immersive) mode.
import SwiftUI
import RealityKit
import UIKit
import GameController

@main
struct OutRunApp: App {
    var body: some SwiftUI.Scene {
        WindowGroup(id: "game") {
            GameView()
        }
        .windowStyle(.plain)
        .defaultSize(width: 1280, height: 960)
    }
}

struct GameView: View {
    @Environment(\.scenePhase) private var phase
    @State private var screen = StereoScreen()

    var body: some View {
        GeometryReader3D { proxy in
            RealityView { content in
                await screen.make(in: content)
            } update: { content in
                screen.fit(proxy.frame(in: .local), size: proxy.size, content: content)
            }
        }
        // A flat window: without this the RealityView takes the window's 3D depth
        // (540 pt), the picture and the window's grip / resize corners 40 cm apart.
        .frame(depth: 0)
        // Game controller input goes to the game (GameController), not to the window as
        // pinches. One claim per window: a layer over the whole picture (visionOS 26),
        // made again on every tap; older visionOS, GCEventInteraction on the window.
        // Two claims (that layer's modifier and the window's interaction) left the
        // controller stuck on the wrong one, the game reading its last state.
        .overlay { GamepadClaim() }
        .background { if #available(visionOS 26.0, *) { EmptyView() } else { GamepadInput() } }
        .onAppear { or2_vision_start() }
        .onChange(of: phase) { _, now in
            trace("[visionos] scene phase \(now)")
            // Held only in the background (window closed or hidden): visionOS makes the
            // scene inactive in ordinary use (system UI, another window focused), where
            // holding the game froze it and dropped the controller's presses.
            or2_vision_set_active(now != .background)
            or2_vision_set_focused(now == .active)
        }
    }
}

@MainActor
final class StereoScreen {
    private let plane = ModelEntity(mesh: .generatePlane(width: 1, height: 1),
                                    materials: [UnlitMaterial(color: .black)])
    private var material: ShaderGraphMaterial?
    private var left: LowLevelTexture?
    private var right: LowLevelTexture?
    private var width = 0, height = 0
    private let queue: MTLCommandQueue? = or2_vision_device().makeCommandQueue()
    private var update: EventSubscription?
    private var aspect: Float = 0
    private var points = CGSize(width: 1280, height: 960)

    func make(in content: RealityViewContent) async {
        do {
            material = try await ShaderGraphMaterial(named: "/Root/Stereo", from: "StereoMaterial.usda", in: Bundle.main)
        } catch {
            print("[visionos] stereo material: \(error)")
        }
        content.add(plane)
        // Every frame the system renders: the game's newest frame into the eyes' textures.
        update = content.subscribe(to: SceneEvents.Update.self) { [weak self] _ in
            MainActor.assumeIsolated { self?.frame() }
        }
    }

    // The plane covers the window, on its surface (the view is flat: depth 0).
    // Its physical width sets the eye separation.
    private var logged = SIMD3<Float>(0, 0, 0)
    func fit(_ frame: Rect3D, size: Size3D, content: RealityViewContent) {
        let box = content.convert(frame, from: .local, to: .scene)
        plane.position = [box.center.x, box.center.y, box.min.z]
        if box.extents != logged {
            logged = box.extents
            trace("[visionos] window \(Int(size.width))x\(Int(size.height))x\(Int(size.depth)) pt, local z \(frame.origin.z)..\(frame.origin.z + frame.size.depth), scene box \(box.min) .. \(box.max), picture at \(plane.position)")
        }
        plane.scale = [max(box.extents.x, 0.01), max(box.extents.y, 0.01), 1]
        or2_vision_set_window_width(box.extents.x)
        points = CGSize(width: size.width, height: size.height)
    }

    // Diagnostics (OR2_TRACE): RealityKit updates (the headset's refreshes) and new game frames per second.
    private let tracing = ProcessInfo.processInfo.environment["OR2_TRACE"] != nil
    private var updates = 0, taken = 0, since = Date()
    private func frame() {
        follow_aspect()
        updates += 1
        if tracing && Date().timeIntervalSince(since) >= 2 {
            trace("[visionos] \(updates / 2) updates/s, \(taken / 2) frames/s")
            updates = 0; taken = 0; since = Date()
        }
        var w: Int32 = 0, h: Int32 = 0
        let slot = or2_vision_take_frame(&w, &h)
        guard slot >= 0 else { return }
        taken += 1
        if Int(w) != width || Int(h) != height { textures(width: Int(w), height: Int(h)) }
        guard let queue, let left, let right, let command = queue.makeCommandBuffer() else {
            or2_vision_release_frame(slot)
            return
        }
        or2_vision_encode_frame(slot, command, left.replace(using: command), right.replace(using: command))
        command.addCompletedHandler { _ in or2_vision_release_frame(slot) }
        command.commit()
    }

    // The eyes' textures at the composed frame's size, bound to the material.
    private func textures(width w: Int, height h: Int) {
        var d = LowLevelTexture.Descriptor()
        d.textureType = .type2D
        d.pixelFormat = .rgba8Unorm_srgb
        d.width = w
        d.height = h
        d.depth = 1
        d.mipmapLevelCount = 1
        d.textureUsage = [.renderTarget, .shaderRead]
        do {
            let l = try LowLevelTexture(descriptor: d)
            let r = try LowLevelTexture(descriptor: d)
            if var m = material {
                try m.setParameter(name: "left", value: .textureResource(try TextureResource(from: l)))
                try m.setParameter(name: "right", value: .textureResource(try TextureResource(from: r)))
                material = m
                plane.model?.materials = [m]
            } else {
                // No stereo material: the left eye alone.
                var m = UnlitMaterial()
                m.color = .init(texture: .init(try TextureResource(from: l)))
                plane.model?.materials = [m]
            }
            left = l
            right = r
            width = w
            height = h
        } catch {
            print("[visionos] eye textures \(w)x\(h): \(error)")
        }
    }

    // The window resizes uniformly at the game's aspect (4:3 or 16:9).
    private func follow_aspect() {
        let a = or2_vision_window_aspect()
        guard a > 0, abs(a - aspect) > 0.001,
              let scene = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first else { return }
        aspect = a
        let ratio = CGFloat(a)
        let h = max(points.height, 480)
        let geometry = UIWindowScene.GeometryPreferences.Vision(
            size: CGSize(width: h * ratio, height: h),
            minimumSize: CGSize(width: 480 * ratio, height: 480),
            maximumSize: CGSize(width: 2400 * ratio, height: 2400),
            resizingRestrictions: .uniform)
        // Not from inside RealityKit's update (UIKit aborts in its drawing fence): next main-queue turn.
        DispatchQueue.main.async {
            scene.requestGeometryUpdate(geometry) { error in
                trace("[visionos] window geometry: \(error)")
            }
            trace("[visionos] asked \(geometry.size ?? .zero) uniform; restrictions min \(scene.sizeRestrictions?.minimumSize ?? .zero) max \(scene.sizeRestrictions?.maximumSize ?? .zero)")
        }
    }
}

// GCEventInteraction on the window: on visionOS the system turns game
// controller input into pinches for the gazed-at view unless the view hosting
// the game asks for it (exclusively here) through the Game Controller framework,
// which SDL's MFi joystick reads.
struct GamepadInput: UIViewRepresentable {
    final class Host: UIView {
        private let interaction: GCEventInteraction = {
            let i = GCEventInteraction()
            i.handledEventTypes = .gamepad
            return i
        }()
        override func didMoveToWindow() {
            super.didMoveToWindow()
            interaction.view?.removeInteraction(interaction)
            if let window {
                window.addInteraction(interaction)
                trace("[visionos] gamepad interaction on window \(window.bounds.size)")
            }
        }
    }
    func makeUIView(context: Context) -> Host { Host() }
    func updateUIView(_ view: Host, context: Context) {}
}

// The controller claim (visionOS 26): a transparent layer over the picture that
// takes the gamepad's events. A tap on the window (a pinch, or a controller press
// that visionOS turned into one because the game wasn't getting it) makes the
// claim again: the layer is rebuilt, so its interaction is registered anew.
struct GamepadClaim: View {
    @State private var claim = 0
    var body: some View {
        Color.clear
            .contentShape(Rectangle())
            .gamepadEvents()
            .onTapGesture {
                claim += 1
                trace("[pad] tap: controller claimed again (\(claim))")
                or2_vision_gamepad_reclaim()
            }
            .id(claim)
    }
}

// Device diagnostics on stderr (unbuffered; Swift's print is buffered in a pipe).
func trace(_ line: String) {
    FileHandle.standardError.write((line + "\n").data(using: .utf8)!)
}

extension View {
    // visionOS 26: the view (all of it, where the player looks) takes the
    // controller's events through GameController.
    @ViewBuilder func gamepadEvents() -> some View {
        if #available(visionOS 26.0, *) {
            self.handlesGameControllerEvents(matching: .gamepad)
        } else {
            self
        }
    }
}

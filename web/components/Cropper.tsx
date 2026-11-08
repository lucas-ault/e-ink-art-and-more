"use client";

import { forwardRef, useEffect, useImperativeHandle, useRef } from "react";
import type { Crop } from "@/lib/api";

const VIEW_W = 360;
const VIEW_H = 480;

export type CropperHandle = { crop: () => Crop };

type Drag = { x: number; y: number; panX: number; panY: number };
type View = {
  zoom: number;
  panX: number;
  panY: number;
  image: HTMLImageElement | null;
  drag: Drag | null;
};

export const Cropper = forwardRef<CropperHandle, { src: string }>(function Cropper({ src }, ref) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const view = useRef<View>({ zoom: 1, panX: 0, panY: 0, image: null, drag: null });

  function draw() {
    const canvas = canvasRef.current;
    const context = canvas?.getContext("2d");
    if (!canvas || !context) return;
    const { image, zoom, panX, panY } = view.current;
    context.setTransform(1, 0, 0, 1, 0, 0);
    context.fillStyle = "#ffffff";
    context.fillRect(0, 0, VIEW_W, VIEW_H);
    if (!image) return;
    context.drawImage(image, panX, panY, VIEW_W / zoom, VIEW_H / zoom, 0, 0, VIEW_W, VIEW_H);
  }

  function clamp() {
    const { image, zoom } = view.current;
    if (!image) return;
    const visibleW = VIEW_W / zoom;
    const visibleH = VIEW_H / zoom;
    view.current.panX = Math.min(Math.max(0, view.current.panX), Math.max(0, image.naturalWidth - visibleW));
    view.current.panY = Math.min(Math.max(0, view.current.panY), Math.max(0, image.naturalHeight - visibleH));
  }

  useImperativeHandle(ref, () => ({
    crop: () => {
      const { zoom, panX, panY } = view.current;
      return { x: panX, y: panY, width: VIEW_W / zoom, height: VIEW_H / zoom };
    },
  }));

  useEffect(() => {
    const image = new Image();
    image.onload = () => {
      const zoom = Math.max(VIEW_W / image.naturalWidth, VIEW_H / image.naturalHeight);
      view.current.image = image;
      view.current.zoom = zoom;
      view.current.panX = (image.naturalWidth - VIEW_W / zoom) / 2;
      view.current.panY = (image.naturalHeight - VIEW_H / zoom) / 2;
      clamp();
      draw();
    };
    image.src = src;
  }, [src]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    function scale() {
      const rect = canvas!.getBoundingClientRect();
      return VIEW_W / rect.width;
    }

    function onDown(event: PointerEvent) {
      if (!view.current.image) return;
      view.current.drag = {
        x: event.clientX,
        y: event.clientY,
        panX: view.current.panX,
        panY: view.current.panY,
      };
      canvas!.setPointerCapture(event.pointerId);
    }

    function onMove(event: PointerEvent) {
      const drag = view.current.drag;
      if (!drag) return;
      const ratio = scale();
      view.current.panX = drag.panX - ((event.clientX - drag.x) * ratio) / view.current.zoom;
      view.current.panY = drag.panY - ((event.clientY - drag.y) * ratio) / view.current.zoom;
      clamp();
      draw();
    }

    function onUp() {
      view.current.drag = null;
    }

    function onWheel(event: WheelEvent) {
      const image = view.current.image;
      if (!image) return;
      event.preventDefault();
      const rect = canvas!.getBoundingClientRect();
      const ratio = VIEW_W / rect.width;
      const cx = (event.clientX - rect.left) * ratio;
      const cy = (event.clientY - rect.top) * (VIEW_H / rect.height);
      const srcX = view.current.panX + cx / view.current.zoom;
      const srcY = view.current.panY + cy / view.current.zoom;
      const factor = event.deltaY < 0 ? 1.08 : 1 / 1.08;
      const floor = Math.max(VIEW_W / image.naturalWidth, VIEW_H / image.naturalHeight);
      view.current.zoom = Math.min(floor * 12, Math.max(floor, view.current.zoom * factor));
      view.current.panX = srcX - cx / view.current.zoom;
      view.current.panY = srcY - cy / view.current.zoom;
      clamp();
      draw();
    }

    canvas.addEventListener("pointerdown", onDown);
    canvas.addEventListener("pointermove", onMove);
    canvas.addEventListener("pointerup", onUp);
    canvas.addEventListener("pointercancel", onUp);
    canvas.addEventListener("wheel", onWheel, { passive: false });
    return () => {
      canvas.removeEventListener("pointerdown", onDown);
      canvas.removeEventListener("pointermove", onMove);
      canvas.removeEventListener("pointerup", onUp);
      canvas.removeEventListener("pointercancel", onUp);
      canvas.removeEventListener("wheel", onWheel);
    };
  }, []);

  return (
    <canvas
      ref={canvasRef}
      width={VIEW_W}
      height={VIEW_H}
      className="aspect-[3/4] w-full max-w-[420px] touch-none border border-line bg-white"
    />
  );
});

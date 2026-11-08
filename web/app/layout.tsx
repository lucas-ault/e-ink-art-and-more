import type { Metadata } from "next";
import { Geist } from "next/font/google";
import { Shell } from "@/components/Shell";
import "./globals.css";

const geist = Geist({
  subsets: ["latin"],
  variable: "--font-geist",
});

export const metadata: Metadata = {
  title: "Panel studio",
  description: "Choose a picture and send it to the panel.",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body className={`${geist.variable} antialiased`}>
        <Shell>{children}</Shell>
      </body>
    </html>
  );
}

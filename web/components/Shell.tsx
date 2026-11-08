"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

const NAV = [
  { href: "/", label: "Currently showing", short: "Showing" },
  { href: "/upload", label: "Upload art", short: "Upload" },
  { href: "/library", label: "Select art", short: "Select" },
  { href: "/settings", label: "Settings", short: "Settings" },
];

function active(pathname: string, href: string) {
  if (href === "/") return pathname === "/";
  return pathname.startsWith(href);
}

export function Shell({ children }: { children: React.ReactNode }) {
  const pathname = usePathname();

  return (
    <div className="min-h-screen">
      <aside className="fixed inset-y-0 left-0 hidden w-56 flex-col border-r border-line bg-card px-4 py-6 md:flex">
        <p className="px-3 text-sm tracking-wide text-muted">Panel studio</p>
        <nav className="mt-8 flex flex-col gap-1">
          {NAV.map((item) => {
            const on = active(pathname, item.href);
            return (
              <Link
                key={item.href}
                href={item.href}
                className={`rounded-md px-3 py-2 text-[15px] ${on ? "bg-ink text-paper" : "text-ink hover:bg-paper"}`}
              >
                {item.label}
              </Link>
            );
          })}
        </nav>
      </aside>

      <main className="mx-auto w-full max-w-5xl px-5 pt-8 pb-28 md:py-10 md:pl-64 md:pr-8">
        {children}
      </main>

      <nav className="fixed inset-x-0 bottom-0 z-10 grid grid-cols-4 border-t border-line bg-card md:hidden">
        {NAV.map((item) => {
          const on = active(pathname, item.href);
          return (
            <Link
              key={item.href}
              href={item.href}
              className={`px-1 py-3 text-center text-xs ${on ? "text-ink font-semibold" : "text-muted"}`}
            >
              {item.short}
            </Link>
          );
        })}
      </nav>
    </div>
  );
}

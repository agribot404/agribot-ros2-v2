import React from 'react';
import { Navbar } from './Navbar';

export const Layout: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  return (
    <div className="w-full min-h-screen p-4 md:p-8 flex flex-col max-w-7xl mx-auto">
      <Navbar />
      <main className="flex-grow flex flex-col gap-6">
        {children}
      </main>
    </div>
  );
};

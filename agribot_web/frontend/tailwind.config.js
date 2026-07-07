/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        'agri-light': '#f0fdf4',
        'agri-panel': 'rgba(255, 255, 255, 0.75)',
        'agri-primary': '#10b981',
        'agri-accent': '#059669',
        'agri-text': '#064e3b'
      }
    },
  },
  plugins: [],
}

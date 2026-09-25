/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        primary: {
          50: '#fef8f3',
          100: '#fceee0',
          200: '#f9d9c0',
          300: '#f5ba8f',
          400: '#f09358',
          500: '#FB923C',
          600: '#ea580c',
          700: '#c2410c',
          800: '#9a3412',
          900: '#7c2d12',
          950: '#431407',
        },
      },
      fontFamily: {
        sans: ['Inter', 'Onest', 'system-ui', 'sans-serif'],
      },
    },
  },
  plugins: [],
}

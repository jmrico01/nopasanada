const path = require("path");
const HtmlWebpackPlugin = require("html-webpack-plugin");

module.exports = {
	entry: {
		dev: "./src/dev.js",
		home: "./src/home.js",
	},
	output: {
		path: __dirname + "/build"
	},
	plugins:[
		new HtmlWebpackPlugin({
			template: path.resolve(__dirname, "public", "dev.html"),
			filename: "public-dev/index.html"
		}),
		new HtmlWebpackPlugin({
			template: path.resolve(__dirname, "public", "home.html"),
			filename: "public/index.html"
		}),
	],
	resolve: {
		alias: {
			components: path.resolve(__dirname, "src", "components"),
			css: path.resolve(__dirname, "public", "css")
		}
	},
	module: {
		rules: [
			{
				test: /\.js$/,
				exclude: /node_modules/,
				use: {
					loader: "babel-loader",
					options: {
						presets: [
							"@babel/preset-env",
							"@babel/preset-react"
						]
					}
				}
			},
			{
				test: /\.css$/,
				use: [
					"style-loader",
					"css-loader"
				]
			}
		]
	},
};

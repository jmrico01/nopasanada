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
			template: path.resolve(__dirname, "src", "dev.html"),
			filename: "public/index.html"
		}),
		new HtmlWebpackPlugin({
			template: path.resolve(__dirname, "src", "home.html"),
			filename: "public-dev/index.html"
		}),
	],
	resolve: {
		alias: {
			components: path.resolve(__dirname, "src", "components")
		}
	},
	module: {
		rules: [{
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
		}]
	},
};

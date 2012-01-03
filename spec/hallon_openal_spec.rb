require 'bundler/setup'
require 'hallon/openal'

describe Hallon::OpenAL do
  let(:klass)  { described_class }
  let(:format) { Hash.new }
  let(:block)  { proc { |size| [] } }

  describe "#initialize" do
    it "should raise an error if not given a block" do
      expect { klass.new(format) }.to raise_error(ArgumentError)
    end

    it "should raise an error if not given a format" do
      expect { klass.new {} }.to raise_error(ArgumentError)
    end

    it "should not raise an error given a format and a block" do
      expect { klass.new(format, &block) }.to_not raise_error
    end
  end
end

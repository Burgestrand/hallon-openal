require 'bundler/setup'
require 'hallon/openal'

describe Hallon::OpenAL do
  let(:klass)  { described_class }
  let(:format) { Hash.new }
  let(:block)  { proc { |size| [] } }
  subject { klass.new(format, &block) }

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

  describe "#start" do
    it "should not raise an error" do
      expect { subject.start }.to_not raise_error
    end
  end

  describe "#stop" do
    it "should not raise an error" do
      expect { subject.stop }.to_not raise_error
    end
  end

  describe "#pause" do
    it "should not raise an error" do
      expect { subject.pause }.to_not raise_error
    end
  end

  describe "#drops" do
    it "should always return zero" do
      subject.drops.should be_zero
    end
  end
end
